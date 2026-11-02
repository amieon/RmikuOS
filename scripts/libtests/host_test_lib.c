/* 用户态基础件宿主回归：memcpy / memset / fread
 *
 * 被测代码全部来自 scripts/libtests/extract.py 从真实源文件抽出的原文，
 * 所以这里不存在"测试和实现对不上"的可能。
 *
 * 为什么这三个要测：
 *   - memcpy/memset 有对齐快路径，越界一个字节就是静默踩内存；
 *   - fread 改成了"批搬 + 大块直读"，缓冲区 pos/end 的记账一旦错，
 *     症状是"少读几个字节"或"内容错位"，而且只在特定长度下出现。
 */
#include <stdio.h>
#include <string.h>
#include <stddef.h>

/* 目标机是 LP64，这里用等价宽度，保证 char*/ 
typedef long isize;
typedef unsigned long usize;

static int g_fail = 0;
static int g_pass = 0;

#define OK(cond, name)                                       \
    do {                                                     \
        if (cond) {                                          \
            g_pass++;                                        \
        } else {                                             \
            g_fail++;                                        \
            printf("  FAIL %s  (line %d)\n", name, __LINE__); \
        }                                                    \
    } while (0)

/* ---- 真实 read() 的桩：从内存里供数据 ---- */
static unsigned char g_src[200000];
static size_t g_src_len = 0;
static size_t g_src_pos = 0;

static isize rk_read(int fd, void *buf, size_t n);
static isize rk_read(int fd, void *buf, size_t n) {
    (void)fd;
    size_t avail = g_src_len - g_src_pos;
    if (avail == 0) return 0;
    if (n > avail) n = avail;
    memcpy(buf, g_src + g_src_pos, n);
    g_src_pos += n;
    return (isize)n;
}

/* 避免覆盖宿主 libc 的 memcpy/memset/fread（否则 printf 内部可能被我们截胡），
 * 只把符号改个名，被抽出的源码文本一个字不动。 */
#define memcpy rk_memcpy
#define memset rk_memset
#define fread  rk_fread
#define read   rk_read
#include "_extracted.inc"
#undef read
#undef fread
#undef memset
#undef memcpy

static void fill_pattern(unsigned char *p, size_t n, unsigned seed) {
    for (size_t i = 0; i < n; i++) {
        seed = seed * 1103515245u + 12345u;
        p[i] = (unsigned char)(seed >> 16);
    }
}

static RK_FILE mk_file(int flags) {
    RK_FILE f;
    rk_memset(&f, 0, sizeof f);
    f.fd = 3;
    f.ungetc = -1;
    f.flags = flags;
    return f;
}

static void src_reset(size_t n, unsigned seed) {
    g_src_len = n;
    g_src_pos = 0;
    fill_pattern(g_src, n, seed);
}

static void test_memcpy(void) {
    static unsigned char src[128], dst[128], ref[128];
    for (int os = 0; os < 16; os++) {
        for (int od = 0; od < 16; od++) {
            for (size_t len = 0; len <= 40; len++) {
                if (os + (int)len > (int)sizeof src || od + (int)len > (int)sizeof dst) continue;
                fill_pattern(src, sizeof src, (unsigned)(os * 31 + od * 7 + (int)len));
                memset(dst, 0xCC, sizeof dst);
                memset(ref, 0xCC, sizeof ref);

                rk_memcpy(dst + od, src + os, len);
                /* 参照：逐字节，并检查两侧金丝雀没被越界写 */
                for (size_t i = 0; i < len; i++) ref[od + i] = src[os + i];

                int same = memcmp(dst, ref, sizeof dst) == 0;
                if (!same) printf("  memcpy mismatch os=%d od=%d len=%u\n", os, od, (unsigned)len);
                OK(same, "memcpy 任意对齐/长度");
            }
        }
    }
}

static void test_memset(void) {
    static unsigned char buf[128], ref[128];
    const int pats[] = {0x00, 0x5A, 0xFF, 0xA5};
    for (int os = 0; os < 16; os++) {
        for (size_t len = 0; len <= 40; len++) {
            for (size_t p = 0; p < sizeof pats / sizeof pats[0]; p++) {
                if (os + (int)len > (int)sizeof buf) continue;
                memset(buf, 0x33, sizeof buf);
                memset(ref, 0x33, sizeof ref);

                rk_memset(buf + os, pats[p], len);
                for (size_t i = 0; i < len; i++) ref[os + i] = (unsigned char)pats[p];

                int same = memcmp(buf, ref, sizeof buf) == 0;
                if (!same) printf("  memset mismatch off=%d len=%u pat=%02x\n", os, (unsigned)len, pats[p]);
                OK(same, "memset 任意对齐/长度/填充值");
            }
        }
    }
}

static void test_fread(void) {
    static unsigned char out[200000];

    /* 1. 一次读完整块 */
    src_reset(1000, 1);
    RK_FILE a = mk_file(_F_READ);
    size_t got = rk_fread(out, 1, 1000, &a);
    OK(got == 1000 && memcmp(out, g_src, 1000) == 0, "fread 整块内容一致");

    /* 2. 分两次读，第二次跨越内部缓冲边界 */
    src_reset(10000, 2);
    RK_FILE b = mk_file(_F_READ);
    size_t n1 = rk_fread(out, 1, 5, &b); /* 触发一次缓冲填充 */
    size_t n2 = rk_fread(out + 5, 1, 9995, &b);
    OK(n1 == 5 && n2 == 9995 && memcmp(out, g_src, 10000) == 0, "fread 跨缓冲边界连续");

    /* 3. 大块请求走直读路径（不经过内部缓冲） */
    src_reset(100000, 3);
    RK_FILE c = mk_file(_F_READ);
    size_t n3 = rk_fread(out, 1, 100000, &c);
    OK(n3 == 100000 && memcmp(out, g_src, 100000) == 0, "fread 大块直读内容一致");

    /* 4. 读不到就停，且置 EOF */
    src_reset(10, 4);
    RK_FILE d = mk_file(_F_READ);
    size_t n4 = rk_fread(out, 1, 100, &d);
    size_t n4b = rk_fread(out, 1, 100, &d);
    OK(n4 == 10 && n4b == 0 && (d.flags & _F_EOF), "fread 到底返回实际字节数并置 EOF");

    /* 5. 返回完整项数（10 字节按 3 字节一项 = 3 项） */
    src_reset(10, 5);
    RK_FILE e = mk_file(_F_READ);
    size_t n5 = rk_fread(out, 3, 4, &e);
    OK(n5 == 3 && memcmp(out, g_src, 9) == 0, "fread 返回完整项数 floor(got/size)");

    /* 6. ungetc 推回去的字符要排在前面 */
    src_reset(4, 6);
    RK_FILE g = mk_file(_F_READ);
    g.ungetc = 0x77;
    size_t n6 = rk_fread(out, 1, 5, &g);
    OK(n6 == 5 && out[0] == 0x77 && memcmp(out + 1, g_src, 4) == 0, "fread 先吐出 ungetc 字符");

    /* 7. 无缓冲流（setbuf(fp,NULL) 之后）走直读 */
    src_reset(3000, 7);
    RK_FILE h = mk_file(_F_READ | _F_UNBUF);
    size_t n7 = rk_fread(out, 1, 3000, &h);
    OK(n7 == 3000 && memcmp(out, g_src, 3000) == 0, "fread 无缓冲模式内容一致");

    /* 8. 非对齐小块循环读：缓冲区剩余字节不能丢也不能重复 */
    src_reset(5000, 8);
    RK_FILE i = mk_file(_F_READ);
    size_t total = 0;
    while (total < 5000) {
        size_t n = rk_fread(out + total, 1, 37, &i);
        if (n == 0) break;
        total += n;
    }
    OK(total == 5000 && memcmp(out, g_src, 5000) == 0, "fread 小块循环累计无丢失/无重复");

    /* 9. 直读与缓冲混用（一次正好一个 BUFSIZ，再读剩下的） */
    src_reset(9000, 9);
    RK_FILE j = mk_file(_F_READ);
    size_t m1 = rk_fread(out, 1, RK_BUFSIZ, &j);
    size_t m2 = rk_fread(out + m1, 1, 9000 - m1, &j);
    OK(m1 + m2 == 9000 && memcmp(out, g_src, 9000) == 0, "fread 直读与缓冲混用不重不漏");

    /* 10. 一次读超过整个文件大小 */
    src_reset(100, 10);
    RK_FILE k = mk_file(_F_READ);
    size_t m3 = rk_fread(out, 1, 150000, &k);
    OK(m3 == 100 && memcmp(out, g_src, 100) == 0, "fread 请求大于文件时返回实际长度");
}

int main(void) {
    printf("[lib] memcpy / memset / fread 宿主回归\n");
    test_memcpy();
    test_memset();
    test_fread();
    printf("[lib] %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
