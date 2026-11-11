
/* exit: POSIX 退出——先冲刷标准流缓冲再退出, 否则短输出(<BUFSIZ)且无换行
 * 会滞留缓冲, 进程退出(直接 syscall)时丢失。
 * weak: runmain.o(-run) 自带强 exit 时让位。 */
#include "../include/file.h"
#include "../include/stdio.h"
#include "../include/io.h"      /* isatty 要 rk_fstat_raw / RK_DEV_CONSOLE */

/* 三个标准流的唯一定义（file.h 里是 extern 声明）。
 * FILE 池(fopen 用)仍每 TU 一份, 但 stdout/stderr 必须跨 TU 共享。 */
FILE _stdin  = {0, {0}, 0, 0, _F_READ, -1};
FILE _stdout = {1, {0}, 0, 0, _F_WRITE, -1};
FILE _stderr = {2, {0}, 0, 0, _F_WRITE | _F_UNBUF, -1};

/* C++ 退出钩子: cpp_runtime.cpp 提供强定义(逆序跑 __cxa_atexit 表)。
 * 这里只作 weak 声明不定义 —— 纯 C 程序解析成 0 会跳过, 也避免同 TU
 * 的 weak 空实现被内联、把强定义架空。 */
extern void __rk_run_cxa_dtors(void) __attribute__((weak));
/* 主线程退出时跑 TSD destructor 轮次(lib/thread.c 提供强定义)。
 * weak: 只链了部分 libc 的目标解析成 0 就跳过，不会因缺符号链接失败。 */
extern void rk_tsd_run_destructors(void) __attribute__((weak));

/* .fini_array 边界由 linker script 的 PROVIDE_HIDDEN 给出;
 * 没有条目时 start == end, 循环 0 次。 */
extern void (*__fini_array_start[])(void);
extern void (*__fini_array_end[])(void);

void exit(int code) __attribute__((weak));
void exit(int code) {
    if (rk_tsd_run_destructors) rk_tsd_run_destructors();
    /* C++ 析构: 先逆序跑 __cxa_atexit 登记, 再逆序跑 .fini_array。
     * 放在 fflush 之前, 析构里的 printf 才会一起被冲出去。 */
    if (__rk_run_cxa_dtors) __rk_run_cxa_dtors();
    long n = __fini_array_end - __fini_array_start;
    while (n > 0) __fini_array_start[--n]();

    fflush(stdout);
    fflush(stderr);
    syscall3(SYS_EXIT, (usize)code, 0, 0);
    for (;;) {}
}

/* assert() 的落点。<assert.h> 里只剩一个声明，故意不 include 任何头，
 * 原因写在 assert.h 顶部：它一拉 stdio.h/stdlib.h，autoconf 的
 * `char fcntl ();` 探测模板就会和真原型撞成 conflicting types，
 * configure 便判"没有 fcntl"，连带走不到 HAVE_FCNTL_O_NONBLOCK。
 * 所以真正的 printf+abort 放在这里 —— 本文件本来就是"libc 的实体符号"落脚处。
 */
void __rk_assert_fail(const char *expr, const char *file, int line) __attribute__((weak));
void __rk_assert_fail(const char *expr, const char *file, int line) {
    fprintf(stderr, "Assertion failed: %s, file %s, line %d\n",
            expr ? expr : "(null)", file ? file : "(null)", line);
    fflush(stderr);
    exit(134);
}

/* C++ 纯虚调用落点(cpp_runtime.cpp 的 __cxa_pure_virtual 转过来)。
 * 原来是 while(1){} —— 无输出、占满一个核、看起来像程序卡死。
 * 这里直接 syscall 退出而不走 exit(): 对象状态已坏, 不该再跑析构。 */
void __rk_pure_virtual_fail(void) {
    fprintf(stderr, "pure virtual method called\n");
    fflush(stderr);
    syscall3(SYS_EXIT, 134, 0, 0);
    for (;;) {}
}

/* errno: POSIX 全局错误码(errno.h 已声明, 这里提供定义)。
 * weak: lua.c 等移植代码自带强定义时优先, 否则用这个(TCC 等)。 */
int errno __attribute__((weak)) = 0;

/* gcc 内建原子 __sync_*: TCC 不支持这些内建 -> 需要真实符号。
 * RmikuOS 单核, 用原子指令实现即可(riscv64: amoswap.w.aq + fence; loongarch64: amswap_db.w + dbar)。
 * lock.h 的 spin_lock/spin_unlock 依赖它们。 */
int __sync_lock_test_and_set(int *ptr, int val) {
    int old;
#ifdef USER_ARCH_RISCV64
    __asm__ volatile("amoswap.w.aq %0, %1, (%2)"
                     : "=&r"(old) : "r"(val), "r"(ptr) : "memory");
#else
    /* LoongArch: amswap_db.w rd, rj, rk (rk=内存地址寄存器)
     * 约束: rd != rj && rd != rk (rd 非 $r0 时), 故输出必须 early-clobber,
     * 否则 GCC 可能让 old 与 val 共用寄存器导致汇编错误。 */
    __asm__ volatile("amswap_db.w %0, %1, %2"
                     : "=&r"(old) : "r"(val), "r"(ptr) : "memory");
#endif
    return old;
}
void __sync_synchronize(void) {
#ifdef USER_ARCH_RISCV64
    __asm__ volatile("fence rw,rw" ::: "memory");
#else
    /* LoongArch 没有 RISC-V 式 "fence rw,rw", 内存屏障指令是 dbar。 */
    __asm__ volatile("dbar 0" ::: "memory");
#endif
}
void __sync_lock_release(int *ptr) {
    __asm__ volatile("" ::: "memory");
    *ptr = 0;
}

/* 链接时补符号；借已有的 amoswap 原子做极短临界区，不再写一套
 * 架构相关汇编（riscv 的 amoadd / loongarch 的 amadd 各写一遍容易埋雷）。 */
static int rk_atomic_guard;
int __sync_fetch_and_add(int *ptr, int val) {
    while (__sync_lock_test_and_set(&rk_atomic_guard, 1)) { }
    int old = *ptr;
    *ptr = old + val;
    __sync_synchronize();
    __sync_lock_release(&rk_atomic_guard);
    return old;
}

/* isatty: 真的去问内核这个 fd 是不是终端。
 * 判据 = 字符设备 且 设备号是控制台 —— /dev/null 也是字符设备, 只看类型分不开。
 * 老实现是 "fd 在 0..2 就是终端" 的谎话, 后果: `cat x | git am` 里 fd0 是管道,
 * isatty 却说 1, git 于是按交互式去读终端。
 * 放 string.c 而不是头文件: 它要 fstat, 而 fs.h 会带进 process.h 的 inline exit,
 * 与下面 crt0 要的 weak exit 撞重定义。io.h 是最底层, 没有这个问题。
 * weak: 个别程序自带强定义时让位。 */
__attribute__((weak)) int isatty(int fd) {
    rk_stat_raw_t k;
    if (fd < 0) {
        errno = EBADF;
        return 0;
    }
    if (rk_sys_ret((isize)rk_fstat_raw(fd, &k)) < 0) {
        return 0;      /* errno 已由内核给出(EBADF) */
    }
    if (k.file_type == RK_STAT_TYPE_CHAR && k.dev == RK_DEV_CONSOLE) return 1;
    errno = ENOTTY;
    return 0;
}


void *memset(void *s, int c, unsigned long n) {
    unsigned char *p = (unsigned char *)s;
    unsigned char b = (unsigned char)c;

    while (n && ((unsigned long)p & 7)) {
        *p++ = b;
        n--;
    }
    if (n >= 8) {
        unsigned long v = 0x0101010101010101UL * (unsigned long)b;
        unsigned long *pw = (unsigned long *)p;
        while (n >= 8) {
            *pw++ = v;
            n -= 8;
        }
        p = (unsigned char *)pw;
    }
    while (n--) {
        *p++ = b;
    }
    return s;
}

void *memcpy(void *dst, const void *src, unsigned long n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    while (n && ((unsigned long)d & 7)) {
        *d++ = *s++;
        n--;
    }
    /* 目的对齐后，源也必须对齐才能按字搬；否则整个尾部退回逐字节 */
    if (n >= 8 && ((unsigned long)s & 7) == 0) {
        unsigned long *dw = (unsigned long *)d;
        const unsigned long *sw = (const unsigned long *)s;
        while (n >= 8) {
            *dw++ = *sw++;
            n -= 8;
        }
        d = (unsigned char *)dw;
        s = (const unsigned char *)sw;
    }
    while (n--) {
        *d++ = *s++;
    }
    return dst;
}