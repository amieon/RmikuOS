/*
 * user/lib/dl.c —— RmikuOS 的 mini ELF 动态加载器。
 *
 * 为什么不需要改内核：主程序继续 -static -no-pie 启动，内核 exec 路径
 * (kernel/src/mm/memory_set.rs 的 PT_INTERP 分支) 根本不参与；.so 是我们自己
 * open + read + 匿名 mmap 的，用到的 syscall 19/20/75 早就在。内核只负责
 * 「把字节放进地址空间、给什么权限」，符号决议和重定位全在用户态。
 *
 * 加载四步（下面按这个顺序实现）：
 *   1. 读整个文件到堆缓冲，扫 program header 算 span
 *   2. 一次匿名 mmap(span, RWX)，按 p_vaddr 把每个 PT_LOAD 拷进去，BSS 清零，
 *      算出 load_bias = base - align_down(min_vaddr, page)
 *   3. 读 PT_DYNAMIC 找 .dynsym/.dynstr/.hash/.rela，逐条改 GOT / 数据指针
 *   4. 跑 DT_INIT 与 .init_array，返回句柄
 *
 * 为什么不用文件 mmap：manager_wrapper.rs:1817 要求 offset % PAGE_SIZE == 0，
 * 但 ELF 只保证 p_vaddr ≡ p_offset (mod p_align)，不保证 p_offset 页对齐。
 * 内核的文件映射本来也是 eager 地 read_at 拷进新帧（不共享文件页），
 * 所以「read 整文件 + 匿名映射 + memcpy」跟它语义等价，还顺带绕开 COW。
 * 页大小走 getpagesize()，不硬编码 4096。
 *
 * 第一版明确边界 —— 遇到一律报错，绝不静默跳过：
 *   - 只接 ET_DYN / ELFCLASS64 / 小端 / 本架构；PT_INTERP 直接拒（那是主程序）
 *   - 只认 SysV DT_HASH。GNU ld 默认产 .gnu.hash，构建 .so 必须加
 *     -Wl,--hash-style=sysv，否则 dlopen 明确报错而不是「符号查不到」
 *   - 只认 DT_RELA(+DT_JMPREL)，不认 DT_REL / 符号版本 / DT_NEEDED / COPY /
 *     IRELATIVE / TLS / 任何静态重定位（HI20、PCREL 等）
 *   - 整段 RWX，没做 W^X 分段、RELRO、ASLR 后处理
 *   - RTLD_LAZY 当 RTLD_NOW 用（一次全解析，不实现 PLT 懒绑定）
 *   - 不搜索 LD_LIBRARY_PATH，path 原样交给 open
 *   - 不支持 dladdr / dlvsym / RTLD_NEXT（dlfcn.h 里也就没声明）
 * 多线程安全：整个 loader 一把 spinlock；.so 的构造函数在**锁内**执行，
 * 所以构造函数里不能再 dlopen（会自锁）。这是第一版的已知限制。
 *
 * @io:begin / @io:end 之间是唯一的 I/O 层，宿主差分 harness 会把这段文本
 * 整体换掉，其余代码逐字不变地编译运行。
 */
#include "rk_elf.h"
#include "rk_dlfcn.h"
#include "io.h"        /* open / read / close */
#include "fs.h"        /* stat / struct stat */
#include "mem.h"       /* mmap / munmap / malloc / free */
#include "string.h"    /* strlen / strcmp / memcpy / memset */
#include "stdio.h"     /* vsnprintf */
#include "unistd.h"    /* getpagesize */
#include "lock.h"
#include <stdarg.h>

#if defined(USER_ARCH_RISCV64)
#define RK_DL_EXPECT_MACH RK_EM_RISCV
#elif defined(USER_ARCH_LOONGARCH64)
#define RK_DL_EXPECT_MACH RK_EM_LOONGARCH
#else
/* 真机两个架构都由 build.py 的 cflags 定义了宏；宿主 harness 也要显式给一个。 */
#error "dl.c requires USER_ARCH_RISCV64 or USER_ARCH_LOONGARCH64"
#endif

#define RK_DL_MAX_IMAGE   (16ULL * 1024 * 1024)
#define RK_DL_MAX_HANDLES 16
#define RK_DL_MAX_EXPORTS 64
#define RK_DL_PATH_MAX    160
#define RK_DL_ERRSZ       256

#define RK_PTR_TO_U64(p) ((rk_elf64_addr)(unsigned long long)(p))
#define RK_U64_TO_PTR(v) ((void *)(unsigned long long)(v))
#define RK_DL_FN(f)      ((void *)(unsigned long long)(void (*)(void))(f))
typedef void (*rk_dl_voidfn)(void);

/* @io:begin */
/* 整文件读入。stat 拿准确大小，read 循环处理短读；提前 EOF 是硬错误，
 * 不然缓冲尾部是未初始化堆内存，后面按 ELF 结构读会读出随机偏移。 */
static int rk_dl_read_file(const char *path, unsigned char **out,
                           unsigned long long *out_size) {
    struct stat st;
    if (stat(path, &st) < 0 || st.st_size == 0) {
        return -1;
    }
    unsigned long long size = (unsigned long long)st.st_size;
    if (size > RK_DL_MAX_IMAGE) {
        return -1;
    }
    int fd = (int)open(path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }
    unsigned char *buf = (unsigned char *)malloc((usize)size);
    if (!buf) {
        close(fd);
        return -1;
    }
    unsigned long long done = 0;
    while (done < size) {
        isize n = read(fd, (char *)(buf + done), (usize)(size - done));
        if (n <= 0) {
            free(buf);
            close(fd);
            return -1;
        }
        done += (unsigned long long)n;
    }
    close(fd);
    *out = buf;
    *out_size = size;
    return 0;
}

/* 一次性 RWX。第一版不做按段 W^X / RELRO，注释见文件头。 */
static void *rk_dl_map_anon(unsigned long long span) {
    return rk_anon_mmap((usize)span, PROT_READ | PROT_WRITE | PROT_EXEC);
}

static void rk_dl_unmap(void *base, unsigned long long span) {
    munmap(base, (usize)span);
}
/* @io:end */

/* ==================== 全局状态（一律放 .c，不放头里的 static inline） ==================== */

typedef struct {
    char  name[64];
    void *addr;
} rk_dl_export;

typedef struct {
    int    used;
    int    refcount;
    int    global;
    char   path[RK_DL_PATH_MAX];

    void          *base;      /* mmap 起点 */
    rk_elf64_addr  bias;      /* load_bias */
    rk_elf64_addr  first;     /* align_down(最小 p_vaddr) */
    unsigned long long span;

    rk_elf64_addr  symtab;    /* 以下都是相对 .so 基址的 vaddr */
    rk_elf64_addr  strtab;
    rk_elf64_addr  hashtab;
    unsigned long long strsz;
    unsigned long long syment;
    unsigned long long nsyms;

    rk_elf64_addr  init_func;
    rk_elf64_addr  init_array;
    unsigned long long init_array_n;
    rk_elf64_addr  fini_func;
    rk_elf64_addr  fini_array;
    unsigned long long fini_array_n;
} rk_dl_handle;

static rk_dl_handle g_handles[RK_DL_MAX_HANDLES];
static rk_dl_export g_exports[RK_DL_MAX_EXPORTS];
static int          g_export_n;
static int          g_builtins_done;
static mutex_t      g_dl_lock = MUTEX_INIT;

/* 双缓冲轮转：dlerror() 返回的字符串要一直有效到下一次 dlerror/失败，
 * 不能「返回同一个 buf 然后立刻清空它」。 */
static char g_dl_err[2][RK_DL_ERRSZ];
static int  g_dl_err_cur;

static int rk_dl_fail(const char *fmt, ...) {
    char *buf = g_dl_err[g_dl_err_cur];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, RK_DL_ERRSZ, fmt, ap);
    va_end(ap);
    return -1;
}

/* ==================== 小工具 ==================== */

static unsigned long long rk_dl_align_down(unsigned long long x,
                                            unsigned long long a) {
    return x & ~(a - 1);
}
static unsigned long long rk_dl_align_up(unsigned long long x,
                                          unsigned long long a) {
    return (x + a - 1) & ~(a - 1);
}

/* 文件缓冲越界检查。所有从 ELF 字节里读结构的地方都必须先过这一关，
 * 否则一个截断/伪造的 .so 就能让 loader 读到映射外。 */
static int rk_dl_file_ok(unsigned long long size, unsigned long long off,
                         unsigned long long len) {
    return off <= size && len <= size - off;
}

/* 相对 .so 基址的 vaddr 是否落在已映射的 [first, first+span) 内 */
static int rk_dl_addr_ok(const rk_dl_handle *h, rk_elf64_addr v,
                         unsigned long long len) {
    unsigned long long end = h->first + h->span;
    return v >= h->first && v <= end && len <= end - v;
}

static void *rk_dl_at(const rk_dl_handle *h, rk_elf64_addr v) {
    return RK_U64_TO_PTR(h->bias + v);
}

/* ELF SysV hash，DT_HASH 的桶索引就是它算的。dlsym 用线性扫 dynsym，
 * 但 hash 仍然是 nsyms 的唯一可靠来源（ELF 没有「符号个数」这个 tag）。 */
static unsigned long rk_dl_elf_hash(const char *name) {
    unsigned long h = 0, g;
    const unsigned char *s = (const unsigned char *)name;
    while (*s) {
        h = (h << 4) + *s++;
        g = h & 0xf0000000UL;
        if (g) h ^= g >> 24;
        h &= ~g;
    }
    return h;
}

/* 带上限的 strlen：dynstr 不保证每个 st_name 后面都还有 NUL，
 * 越界读会一路扫到映射末尾。 */
static unsigned long long rk_dl_strnlen(const char *s, unsigned long long max) {
    unsigned long long n = 0;
    while (n < max && s[n]) n++;
    return n;
}

static const char *rk_dl_str(const rk_dl_handle *h, unsigned int off) {
    if ((unsigned long long)off >= h->strsz) return "";
    const char *s = (const char *)rk_dl_at(h, h->strtab + off);
    unsigned long long n = rk_dl_strnlen(s, h->strsz - off);
    return n < h->strsz - off ? s : "";
}

/* ==================== 步骤 1：扫 program header，算要映射多大 ==================== */

typedef struct {
    unsigned short machine;
    rk_elf64_addr  first;
    unsigned long long span;
    unsigned long long phoff;
    unsigned short phentsize;
    unsigned short phnum;
} rk_dl_elf_meta;

static int rk_dl_probe(const unsigned char *img, unsigned long long size,
                       unsigned long long page, rk_dl_elf_meta *m) {
    if (!rk_dl_file_ok(size, 0, sizeof(rk_elf64_ehdr))) {
        return rk_dl_fail("dlopen: file too small for ELF64 header");
    }
    rk_elf64_ehdr eh;
    memcpy(&eh, img, sizeof(eh));

    unsigned int magic;
    memcpy(&magic, eh.e_ident, 4);
    if (magic != RK_ELF_MAGIC32 || eh.e_ident[RK_EI_CLASS] != RK_ELFCLASS64 ||
        eh.e_ident[RK_EI_DATA] != RK_ELFDATA2LSB ||
        eh.e_ident[RK_EI_VERSION] != RK_EV_CURRENT) {
        return rk_dl_fail("dlopen: not a little-endian ELF64 object");
    }
    if (eh.e_type != RK_ET_DYN) {
        return rk_dl_fail("dlopen: e_type=%u, expected ET_DYN(3)", eh.e_type);
    }
    if (eh.e_machine != RK_DL_EXPECT_MACH) {
        return rk_dl_fail("dlopen: e_machine=%u, expected %u for this build",
                          eh.e_machine, (unsigned)RK_DL_EXPECT_MACH);
    }
    if (eh.e_phentsize < sizeof(rk_elf64_phdr) || eh.e_phnum == 0 ||
        !rk_dl_file_ok(size, eh.e_phoff,
                       (unsigned long long)eh.e_phnum * eh.e_phentsize)) {
        return rk_dl_fail("dlopen: bad program header table");
    }

    m->machine   = eh.e_machine;
    m->phoff     = eh.e_phoff;
    m->phentsize = eh.e_phentsize;
    m->phnum     = eh.e_phnum;

    rk_elf64_addr min_v = ~(rk_elf64_addr)0;
    rk_elf64_addr max_v = 0;
    int nload = 0;
    for (unsigned short i = 0; i < eh.e_phnum; i++) {
        rk_elf64_phdr ph;
        memcpy(&ph, img + eh.e_phoff + (unsigned long long)i * eh.e_phentsize,
               sizeof(ph));
        if (ph.p_type == RK_PT_INTERP) {
            return rk_dl_fail("dlopen: PT_INTERP present (that is an executable, not a .so)");
        }
        if (ph.p_type != RK_PT_LOAD) continue;
        if (ph.p_memsz < ph.p_filesz ||
            !rk_dl_file_ok(size, ph.p_offset, ph.p_filesz) ||
            ph.p_vaddr + ph.p_memsz < ph.p_vaddr) {
            return rk_dl_fail("dlopen: PT_LOAD[%u] out of file / overflowing", i);
        }
        if (ph.p_vaddr < min_v) min_v = ph.p_vaddr;
        if (ph.p_vaddr + ph.p_memsz > max_v) max_v = ph.p_vaddr + ph.p_memsz;
        nload++;
    }
    if (!nload) {
        return rk_dl_fail("dlopen: no PT_LOAD segments");
    }
    m->first = rk_dl_align_down(min_v, page);
    m->span  = rk_dl_align_up(max_v, page) - m->first;
    if (m->span == 0 || m->span > RK_DL_MAX_IMAGE) {
        return rk_dl_fail("dlopen: image span %llu out of range", m->span);
    }
    return 0;
}

/* ==================== 符号查找 ==================== */

static int rk_dl_lookup_local(rk_dl_handle *h, const char *name,
                              rk_elf64_addr *out) {
    if (!h->hashtab || !h->symtab || !h->nsyms) return 0;
    unsigned int nbucket, nchain;
    memcpy(&nbucket, rk_dl_at(h, h->hashtab), 4);
    memcpy(&nchain,  rk_dl_at(h, h->hashtab + 4), 4);
    if (!nbucket || !nchain || nchain > h->nsyms) return 0;

    /* DT_HASH 布局：[nbucket][nchain][bucket[nbucket]][chain[nchain]]。
     * 从 bucket[hash % nbucket] 起顺 chain 走，0 = STN_UNDEF = 链尾。 */
    const unsigned int *bucket = (const unsigned int *)
        RK_U64_TO_PTR(h->bias + h->hashtab + 8);
    const unsigned int *chain = bucket + nbucket;
    unsigned int idx = bucket[rk_dl_elf_hash(name) % nbucket];
    unsigned long long guard = 0;
    while (idx != 0 && idx < h->nsyms && guard++ <= h->nsyms) {
        rk_elf64_addr at = h->symtab + (rk_elf64_addr)idx * h->syment;
        if (!rk_dl_addr_ok(h, at, sizeof(rk_elf64_sym))) break;
        rk_elf64_sym sym;
        memcpy(&sym, rk_dl_at(h, at), sizeof(sym));
        if (sym.st_shndx != RK_SHN_UNDEF &&
            RK_STB(sym.st_info) != RK_STB_LOCAL &&
            (unsigned long long)sym.st_name < h->strsz &&
            strcmp(rk_dl_str(h, sym.st_name), name) == 0) {
            *out = (sym.st_shndx == RK_SHN_ABS) ? sym.st_value
                                                : h->bias + sym.st_value;
            return 1;
        }
        idx = chain[idx];   /* guard 挡住伪造 hash 表里的自环 */
    }
    return 0;
}

/* 内置符号：主程序是 -static -no-pie，进程读不到自己 ELF 的 .dynsym，
 * 所以只能显式登记。取 static inline 函数的地址会生成本 TU 的副本，
 * .so 跳过来调用是完全正确的。 */
static void rk_dl_builtins(void) {
    if (g_builtins_done) return;
    static const struct { const char *n; void *a; } tab[] = {
        {"memcpy",    RK_DL_FN(memcpy)},
        {"memset",    RK_DL_FN(memset)},
        {"memcmp",    RK_DL_FN(memcmp)},
        {"strlen",    RK_DL_FN(strlen)},
        {"strcmp",    RK_DL_FN(strcmp)},
        {"strncmp",   RK_DL_FN(strncmp)},
        {"strcpy",    RK_DL_FN(strcpy)},
        {"strdup",    RK_DL_FN(strdup)},
        {"strerror",  RK_DL_FN(strerror)},
        {"malloc",    RK_DL_FN(malloc)},
        {"free",      RK_DL_FN(free)},
        {"calloc",    RK_DL_FN(calloc)},
        {"printf",    RK_DL_FN(printf)},
        {"puts",      RK_DL_FN(puts)},
        {"snprintf",  RK_DL_FN(snprintf)},
        {"open",      RK_DL_FN(open)},
        {"read",      RK_DL_FN(read)},
        {"close",     RK_DL_FN(close)},
        {"write",     RK_DL_FN(write)},
        {"getpagesize", RK_DL_FN(getpagesize)},
    };
    for (unsigned int i = 0; i < sizeof(tab) / sizeof(tab[0]); i++) {
        if (g_export_n >= RK_DL_MAX_EXPORTS) break;
        g_exports[g_export_n].addr = tab[i].a;
        unsigned int k = 0;
        while (k < sizeof(g_exports[g_export_n].name) - 1 && tab[i].n[k]) {
            g_exports[g_export_n].name[k] = tab[i].n[k];
            k++;
        }
        g_exports[g_export_n].name[k] = 0;
        g_export_n++;
    }
    g_builtins_done = 1;
}

static int rk_dl_lookup_global(const char *name, rk_elf64_addr *out) {
    rk_dl_builtins();
    for (int i = 0; i < g_export_n; i++) {
        if (strcmp(g_exports[i].name, name) == 0) {
            *out = RK_PTR_TO_U64(g_exports[i].addr);
            return 1;
        }
    }
    /* RTLD_GLOBAL 打开过的对象也进入全局作用域（不含当前正在加载的那个，
     * 它自己的定义已由 rk_dl_lookup_local 分支处理）。 */
    for (int i = 0; i < RK_DL_MAX_HANDLES; i++) {
        if (!g_handles[i].used || !g_handles[i].global) continue;
        if (rk_dl_lookup_local(&g_handles[i], name, out)) return 1;
    }
    return 0;
}

/* 重定位里 sym 指向的符号：本对象已定义就直接取，否则查全局表。
 * 弱未定义按 POSIX 解析成 0，不能报「找不到符号」—— glibc 生成的
 * .so 常带 __cxa_finalize 这类弱引用。 */
static int rk_dl_resolve(rk_dl_handle *h, unsigned int idx,
                         rk_elf64_addr *out) {
    if (idx >= h->nsyms) {
        return rk_dl_fail("dlopen: symbol index %u >= %llu", idx, h->nsyms);
    }
    rk_elf64_addr at = h->symtab + (rk_elf64_addr)idx * h->syment;
    if (!rk_dl_addr_ok(h, at, sizeof(rk_elf64_sym))) {
        return rk_dl_fail("dlopen: .dynsym[%u] outside image", idx);
    }
    rk_elf64_sym sym;
    memcpy(&sym, rk_dl_at(h, at), sizeof(sym));

    if (sym.st_shndx != RK_SHN_UNDEF) {
        *out = (sym.st_shndx == RK_SHN_ABS)
             ? sym.st_value : h->bias + sym.st_value;
        return 0;
    }
    const char *name = rk_dl_str(h, sym.st_name);
    if (rk_dl_lookup_global(name, out)) return 0;
    if (RK_STB(sym.st_info) == RK_STB_WEAK) {
        *out = 0;
        return 0;
    }
    return rk_dl_fail("dlopen: undefined symbol '%s' (use rk_dl_register)", name);
}

/* ==================== 步骤 3：重定位 ==================== */

/* 三套写回动作。两个架构各自把 psABI 的 type 映射到这些动作，
 * 故意不写成一个共用 switch：IRELATIVE 是 58(RISC-V) / 12(LoongArch)，
 * 合并之后迟早把一边的类型当另一边处理。 */
#define RK_DL_K_NONE    0
#define RK_DL_K_REL     1   /* bias + addend */
#define RK_DL_K_SYM     2   /* sym，丢掉 addend（JUMP_SLOT） */
#define RK_DL_K_SYM_ADD 3   /* sym + addend（64 / GLOB_DAT） */

static int rk_dl_kind_riscv(unsigned int t) {
    switch (t) {
    case RK_R_RISCV_NONE:      return RK_DL_K_NONE;
    case RK_R_RISCV_RELATIVE:  return RK_DL_K_REL;
    case RK_R_RISCV_JUMP_SLOT: return RK_DL_K_SYM;
    case RK_R_RISCV_64:        return RK_DL_K_SYM_ADD;
    default:                   return -1;
    }
}
static int rk_dl_kind_loongarch(unsigned int t) {
    switch (t) {
    case RK_R_LARCH_NONE:      return RK_DL_K_NONE;
    case RK_R_LARCH_RELATIVE:  return RK_DL_K_REL;
    case RK_R_LARCH_JUMP_SLOT: return RK_DL_K_SYM;
    /* LoongArch 没有 GLOB_DAT：TCC loongarch64-link.c:105 就是
     * `#define R_GLOB_DAT R_LARCH_64`，GOT 数据项走 64。 */
    case RK_R_LARCH_64:        return RK_DL_K_SYM_ADD;
    default:                   return -1;
    }
}
static int rk_dl_kind(unsigned short machine, unsigned int t) {
    if (machine == RK_EM_RISCV)     return rk_dl_kind_riscv(t);
    if (machine == RK_EM_LOONGARCH) return rk_dl_kind_loongarch(t);
    return -1;
}

static int rk_dl_reloc_table(rk_dl_handle *h, rk_elf64_addr tab_vaddr,
                             unsigned long long bytes, unsigned short machine) {
    if (!bytes) return 0;
    if (bytes % sizeof(rk_elf64_rela) != 0 ||
        !rk_dl_addr_ok(h, tab_vaddr, bytes)) {
        return rk_dl_fail("dlopen: relocation table outside image / misaligned");
    }
    unsigned long long n = bytes / sizeof(rk_elf64_rela);
    const unsigned char *tab = (const unsigned char *)rk_dl_at(h, tab_vaddr);
    for (unsigned long long i = 0; i < n; i++) {
        rk_elf64_rela r;
        memcpy(&r, tab + i * sizeof(r), sizeof(r));
        unsigned int type = RK_ELF64_R_TYPE(r.r_info);
        unsigned int sym  = RK_ELF64_R_SYM(r.r_info);
        int kind = rk_dl_kind(machine, type);
        if (kind < 0) {
            return rk_dl_fail("dlopen: unsupported relocation type %u (machine %u)",
                              type, machine);
        }
        if (kind == RK_DL_K_NONE) continue;
        if (!rk_dl_addr_ok(h, r.r_offset, sizeof(rk_elf64_addr))) {
            return rk_dl_fail("dlopen: r_offset %llu outside image", r.r_offset);
        }
        rk_elf64_addr val = 0;
        switch (kind) {
        case RK_DL_K_REL:
            val = h->bias + (unsigned long long)r.r_addend;
            break;
        case RK_DL_K_SYM:
            if (rk_dl_resolve(h, sym, &val) < 0) return -1;
            break;
        case RK_DL_K_SYM_ADD:
            if (rk_dl_resolve(h, sym, &val) < 0) return -1;
            val += (unsigned long long)r.r_addend;
            break;
        default:
            break;
        }
        /* 一次性写 8 字节。这里就是「改 GOT」那一步：表在 RW 数据页里，
         * 一次普通 store 就够，内核完全不参与。 */
        memcpy(RK_U64_TO_PTR(h->bias + r.r_offset), &val, sizeof(val));
    }
    return 0;
}

/* ==================== 步骤 2+3：拷段、解析 dynamic、重定位 ==================== */

static int rk_dl_map_image(const unsigned char *img, unsigned long long size,
                           void *base, const rk_dl_elf_meta *m,
                           rk_dl_handle *h) {
    h->base  = base;
    h->first = m->first;
    h->span  = m->span;
    h->bias  = RK_PTR_TO_U64(base) - m->first;

    for (unsigned short i = 0; i < m->phnum; i++) {
        rk_elf64_phdr ph;
        memcpy(&ph, img + m->phoff + (unsigned long long)i * m->phentsize,
               sizeof(ph));
        if (ph.p_type != RK_PT_LOAD) continue;
        unsigned char *dst = (unsigned char *)RK_U64_TO_PTR(h->bias + ph.p_vaddr);
        memcpy(dst, img + ph.p_offset, (usize)ph.p_filesz);
        if (ph.p_memsz > ph.p_filesz) {
            memset(dst + ph.p_filesz, 0, (usize)(ph.p_memsz - ph.p_filesz));
        }
    }

    /* ---- 解析 PT_DYNAMIC：d_val 全是相对 .so 基址的 vaddr，先收集再加 bias ---- */
    rk_elf64_addr dyn_vaddr = 0;
    unsigned long long dyn_bytes = 0;
    for (unsigned short i = 0; i < m->phnum; i++) {
        rk_elf64_phdr ph;
        memcpy(&ph, img + m->phoff + (unsigned long long)i * m->phentsize,
               sizeof(ph));
        if (ph.p_type == RK_PT_DYNAMIC) {
            dyn_vaddr = ph.p_vaddr;
            dyn_bytes = ph.p_memsz;
            break;
        }
    }
    if (!dyn_vaddr || !rk_dl_addr_ok(h, dyn_vaddr, dyn_bytes)) {
        return rk_dl_fail("dlopen: missing / out-of-image PT_DYNAMIC");
    }

    rk_elf64_addr rela = 0, jmprel = 0;
    unsigned long long relasz = 0, pltrelsz = 0, relaent = 0;
    unsigned long long initarr = 0, initarrsz = 0;
    unsigned long long finiarr = 0, finiarrsz = 0;
    int have_hash = 0, have_rela = 0, have_jmprel = 0;
    unsigned long long pltrel = 0;

    unsigned long long ndyn = dyn_bytes / sizeof(rk_elf64_dyn);
    const unsigned char *dyn = (const unsigned char *)rk_dl_at(h, dyn_vaddr);
    for (unsigned long long i = 0; i < ndyn; i++) {
        rk_elf64_dyn d;
        memcpy(&d, dyn + i * sizeof(d), sizeof(d));
        if (d.d_tag == RK_DT_NULL) break;
        switch (d.d_tag) {
        /* 下面这些「不支持」的必须显式拒绝：跳过一个 DT_NEEDED，
         * 表现就是 .so 里的外部符号永远解析不到，还查不出原因。 */
        case RK_DT_NEEDED:
            return rk_dl_fail("dlopen: DT_NEEDED unsupported (no .so dependencies)");
        case RK_DT_GNU_HASH:
            return rk_dl_fail("dlopen: DT_GNU_HASH unsupported; relink with -Wl,--hash-style=sysv");
        case RK_DT_REL: case RK_DT_RELSZ: case RK_DT_RELENT:
            return rk_dl_fail("dlopen: DT_REL unsupported (only DT_RELA)");
        case RK_DT_VERSYM: case RK_DT_VERDEF: case RK_DT_VERNEED:
            return rk_dl_fail("dlopen: symbol versioning unsupported");
        case RK_DT_TEXTREL:
            return rk_dl_fail("dlopen: DT_TEXTREL unsupported");
        case RK_DT_HASH:     h->hashtab = d.d_val; have_hash = 1; break;
        case RK_DT_SYMTAB:   h->symtab  = d.d_val; break;
        case RK_DT_STRTAB:   h->strtab  = d.d_val; break;
        case RK_DT_STRSZ:    h->strsz   = d.d_val; break;
        case RK_DT_SYMENT:   h->syment  = d.d_val; break;
        case RK_DT_RELA:     rela     = d.d_val; have_rela   = 1; break;
        case RK_DT_RELASZ:   relasz   = d.d_val; break;
        case RK_DT_RELAENT:  relaent  = d.d_val; break;
        case RK_DT_JMPREL:   jmprel   = d.d_val; have_jmprel = 1; break;
        case RK_DT_PLTRELSZ: pltrelsz = d.d_val; break;
        case RK_DT_PLTREL:   pltrel   = d.d_val; break;
        case RK_DT_INIT:        h->init_func  = d.d_val; break;
        case RK_DT_INIT_ARRAY:  initarr   = d.d_val; break;
        case RK_DT_INIT_ARRAYSZ:initarrsz = d.d_val; break;
        case RK_DT_FINI:        h->fini_func  = d.d_val; break;
        case RK_DT_FINI_ARRAY:  finiarr   = d.d_val; break;
        case RK_DT_FINI_ARRAYSZ:finiarrsz = d.d_val; break;
        default: break;
        }
    }

    if (!have_hash) {
        return rk_dl_fail("dlopen: no DT_HASH (relink with -Wl,--hash-style=sysv)");
    }
    if (!h->symtab || !h->strtab || !h->strsz) {
        return rk_dl_fail("dlopen: missing DT_SYMTAB / DT_STRTAB / DT_STRSZ");
    }
    if (h->syment != sizeof(rk_elf64_sym)) {
        return rk_dl_fail("dlopen: DT_SYMENT=%llu, expected 24", h->syment);
    }
    if (relaent && relaent != sizeof(rk_elf64_rela)) {
        return rk_dl_fail("dlopen: DT_RELAENT=%llu, expected 24", relaent);
    }
    if (!rk_dl_addr_ok(h, h->hashtab, 8) ||
        !rk_dl_addr_ok(h, h->strtab, h->strsz)) {
        return rk_dl_fail("dlopen: DT_HASH / DT_STRTAB outside image");
    }
    /* nchain 就是 .dynsym 的条目数 —— 重定位要校验 sym 下标、dlsym 要遍历，
     * 都靠这个数，所以它必须和 symtab 的实际范围对得上。 */
    unsigned int hb[2];
    memcpy(hb, rk_dl_at(h, h->hashtab), sizeof(hb));
    unsigned long long hash_bytes = 8ULL + (unsigned long long)hb[0] * 4
                                        + (unsigned long long)hb[1] * 4;
    if (!rk_dl_addr_ok(h, h->hashtab, hash_bytes) ||
        !rk_dl_addr_ok(h, h->symtab, (unsigned long long)hb[1] * h->syment)) {
        return rk_dl_fail("dlopen: DT_HASH buckets/chains outside image");
    }
    h->nsyms = hb[1];

    if (initarrsz) {
        if (!rk_dl_addr_ok(h, initarr, initarrsz) || initarrsz % 8) {
            return rk_dl_fail("dlopen: DT_INIT_ARRAY outside image / misaligned");
        }
        h->init_array = initarr;
        h->init_array_n = initarrsz / 8;
    }
    if (finiarrsz) {
        if (!rk_dl_addr_ok(h, finiarr, finiarrsz) || finiarrsz % 8) {
            return rk_dl_fail("dlopen: DT_FINI_ARRAY outside image / misaligned");
        }
        h->fini_array = finiarr;
        h->fini_array_n = finiarrsz / 8;
    }
    if (relasz && !have_rela) {
        return rk_dl_fail("dlopen: DT_RELASZ without DT_RELA");
    }
    if (pltrelsz && !have_jmprel) {
        return rk_dl_fail("dlopen: DT_PLTRELSZ without DT_JMPREL");
    }
    if (pltrelsz && pltrel != RK_DT_RELA) {
        return rk_dl_fail("dlopen: DT_PLTREL=%llu, expected DT_RELA(7)", pltrel);
    }
    if (rk_dl_reloc_table(h, rela, relasz, m->machine) < 0) return -1;
    if (rk_dl_reloc_table(h, jmprel, pltrelsz, m->machine) < 0) return -1;
    return 0;
}

/* ==================== 步骤 4：构造 / 析构 ==================== */

static void rk_dl_run_init(const rk_dl_handle *h) {
    if (h->init_func) ((rk_dl_voidfn)(unsigned long long)(h->bias + h->init_func))();
    for (unsigned long long i = 0; i < h->init_array_n; i++) {
        rk_elf64_addr fn;
        memcpy(&fn, RK_U64_TO_PTR(h->bias + h->init_array + i * 8), 8);
        if (fn) ((rk_dl_voidfn)(unsigned long long)fn)();
    }
}

static void rk_dl_run_fini(const rk_dl_handle *h) {
    for (unsigned long long i = h->fini_array_n; i > 0; i--) {
        rk_elf64_addr fn;
        memcpy(&fn, RK_U64_TO_PTR(h->bias + h->fini_array + (i - 1) * 8), 8);
        if (fn) ((rk_dl_voidfn)(unsigned long long)fn)();
    }
    if (h->fini_func) ((rk_dl_voidfn)(unsigned long long)(h->bias + h->fini_func))();
}

static rk_dl_handle *rk_dl_find_handle(void *p) {
    for (int i = 0; i < RK_DL_MAX_HANDLES; i++) {
        if (g_handles[i].used && (void *)&g_handles[i] == p) return &g_handles[i];
    }
    return (rk_dl_handle *)0;
}

/* ==================== 四个 POSIX 入口 ==================== */

void *dlopen(const char *path, int flags) {
    if (!path || !path[0]) {
        rk_dl_fail("dlopen: empty path");
        return (void *)0;
    }
    if (strlen(path) >= RK_DL_PATH_MAX) {
        rk_dl_fail("dlopen: path too long");
        return (void *)0;
    }
    if (flags & ~(RTLD_LAZY | RTLD_NOW | RTLD_GLOBAL | RTLD_LOCAL)) {
        rk_dl_fail("dlopen: unsupported flags 0x%x", flags);
        return (void *)0;
    }

    mutex_lock(&g_dl_lock);

    /* 同一路径重复 dlopen：复用映射，只加引用计数（标准行为）。 */
    for (int i = 0; i < RK_DL_MAX_HANDLES; i++) {
        rk_dl_handle *h = &g_handles[i];
        if (!h->used || strcmp(h->path, path) != 0) continue;
        h->refcount++;
        if (flags & RTLD_GLOBAL) h->global = 1;
        mutex_unlock(&g_dl_lock);
        return (void *)h;
    }

    int slot = -1;
    for (int i = 0; i < RK_DL_MAX_HANDLES; i++) {
        if (!g_handles[i].used) { slot = i; break; }
    }
    if (slot < 0) {
        rk_dl_fail("dlopen: too many loaded objects (max %d)", RK_DL_MAX_HANDLES);
        mutex_unlock(&g_dl_lock);
        return (void *)0;
    }

    unsigned char *img = (unsigned char *)0;
    unsigned long long size = 0;
    if (rk_dl_read_file(path, &img, &size) < 0) {
        rk_dl_fail("dlopen: cannot read '%s' (errno=%d)", path, errno);
        mutex_unlock(&g_dl_lock);
        return (void *)0;
    }

    unsigned long long page = (unsigned long long)getpagesize();
    rk_dl_elf_meta meta;
    if (rk_dl_probe(img, size, page, &meta) < 0) {
        free(img);
        mutex_unlock(&g_dl_lock);
        return (void *)0;
    }

    void *base = rk_dl_map_anon(meta.span);
    if (base == MAP_FAILED || !base) {
        rk_dl_fail("dlopen: mmap(%llu) failed (errno=%d)", meta.span, errno);
        free(img);
        mutex_unlock(&g_dl_lock);
        return (void *)0;
    }

    rk_dl_handle *h = &g_handles[slot];
    memset(h, 0, sizeof(*h));
    {
        unsigned int k = 0;
        while (path[k]) { h->path[k] = path[k]; k++; }
        h->path[k] = 0;
    }
    if (rk_dl_map_image(img, size, base, &meta, h) < 0) {
        rk_dl_unmap(base, meta.span);
        free(img);
        memset(h, 0, sizeof(*h));
        mutex_unlock(&g_dl_lock);
        return (void *)0;
    }
    free(img);   /* 所有内容已拷进映射，dynsym/dynstr 从映射区读 */

    h->used     = 1;
    h->refcount = 1;
    h->global   = (flags & RTLD_GLOBAL) != 0;
    rk_dl_run_init(h);
    mutex_unlock(&g_dl_lock);
    return (void *)h;
}

void *dlsym(void *handle, const char *name) {
    if (!name || !name[0]) {
        rk_dl_fail("dlsym: empty symbol name");
        return (void *)0;
    }
    mutex_lock(&g_dl_lock);
    rk_dl_builtins();
    rk_elf64_addr v = 0;
    int found = 0;
    if (handle && handle != RTLD_DEFAULT) {
        rk_dl_handle *h = rk_dl_find_handle(handle);
        if (!h) {
            rk_dl_fail("dlsym: invalid handle");
            mutex_unlock(&g_dl_lock);
            return (void *)0;
        }
        found = rk_dl_lookup_local(h, name, &v);
        if (!found) found = rk_dl_lookup_global(name, &v);
    } else {
        found = rk_dl_lookup_global(name, &v);
    }
    if (!found) {
        rk_dl_fail("dlsym: symbol '%s' not found", name);
        mutex_unlock(&g_dl_lock);
        return (void *)0;
    }
    mutex_unlock(&g_dl_lock);
    return RK_U64_TO_PTR(v);
}

int dlclose(void *handle) {
    mutex_lock(&g_dl_lock);
    rk_dl_handle *h = rk_dl_find_handle(handle);
    if (!h) {
        rk_dl_fail("dlclose: invalid handle");
        mutex_unlock(&g_dl_lock);
        return -1;
    }
    if (--h->refcount > 0) {
        mutex_unlock(&g_dl_lock);
        return 0;
    }
    rk_dl_run_fini(h);
    void *base = h->base;
    unsigned long long span = h->span;
    memset(h, 0, sizeof(*h));
    mutex_unlock(&g_dl_lock);
    /* 解映射放在锁外：munmap 会进内核，没必要让别的线程等。 */
    rk_dl_unmap(base, span);
    return 0;
}

char *dlerror(void) {
    char *s = g_dl_err[g_dl_err_cur];
    if (!s[0]) return (char *)0;
    g_dl_err_cur ^= 1;
    g_dl_err[g_dl_err_cur][0] = 0;
    return s;
}

int rk_dl_register(const char *name, void *addr) {
    if (!name || !name[0] || !addr) {
        rk_dl_fail("rk_dl_register: bad argument");
        return -1;
    }
    if (strlen(name) >= sizeof(g_exports[0].name)) {
        rk_dl_fail("rk_dl_register: name too long");
        return -1;
    }
    mutex_lock(&g_dl_lock);
    rk_dl_builtins();
    for (int i = 0; i < g_export_n; i++) {
        if (strcmp(g_exports[i].name, name) == 0) {
            g_exports[i].addr = addr;
            mutex_unlock(&g_dl_lock);
            return 0;
        }
    }
    if (g_export_n >= RK_DL_MAX_EXPORTS) {
        rk_dl_fail("rk_dl_register: export table full");
        mutex_unlock(&g_dl_lock);
        return -1;
    }
    unsigned int k = 0;
    while (name[k]) { g_exports[g_export_n].name[k] = name[k]; k++; }
    g_exports[g_export_n].name[k] = 0;
    g_exports[g_export_n].addr = addr;
    g_export_n++;
    mutex_unlock(&g_dl_lock);
    return 0;
}
