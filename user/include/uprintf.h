#ifndef UPRINTF_H
#define UPRINTF_H

/*
 * uprintf.h —— 极简用户态 printf。
 *
 * 设计目标：
 *   - 不依赖任何 libc，只依赖你已有的 write(1, buf, len) 系统调用包装。
 *   - 在栈上把字符串拼好，攒满一块缓冲再一次性 write 出去，
 *     而不是每个字符 / 每个片段都做一次系统调用。
 *   - no_std / freestanding 友好：只用编译器自带的 <stdarg.h>。
 *
 * 用法：
 *   #include "user.h"     // 提供 write / usize / isize 等
 *   #include "uprintf.h"
 *
 *   uprintf("alpha=%d pid=%u name=%s late=%lu hex=%x\n",
 *           alpha, (unsigned)pid, "control", (usize)late, flags);
 *
 * 支持的格式：
 *   %d   int（带符号十进制）
 *   %u   unsigned int（无符号十进制）
 *   %ld  long / isize（带符号十进制）
 *   %lu  unsigned long / usize（无符号十进制）
 *   %x   unsigned int（小写十六进制，无 0x 前缀）
 *   %lx  unsigned long / usize（小写十六进制）
 *   %p   指针（带 0x 前缀的十六进制）
 *   %c   字符
 *   %s   C 字符串（NULL 安全，打印 "(null)"）
 *   %%   字面量 '%'
 *
 * 故意不支持宽度 / 精度 / 补零（如 %08x）。需要的话再加。
 *
 * 依赖：本文件假定 write / usize / isize 由前面 include 的头（如 user.h）提供。
 */

#include <stdarg.h>
#include "sys.h"

/*
 * 单次拼接缓冲大小。打印内容超过它时，会在中途自动 flush，
 * 因此“一次 write”是常态、不是硬保证；长字符串会拆成多次 write。
 */
#ifndef UPRINTF_BUF_SIZE
#define UPRINTF_BUF_SIZE 1024
#endif

struct uprintf_buf {
    char data[UPRINTF_BUF_SIZE];
    int  len;
};

static inline void uprintf_flush(struct uprintf_buf *b) {
    if (b->len > 0) {
        write(1, b->data, (usize)b->len);
        b->len = 0;
    }
}

static inline void uprintf_putc(struct uprintf_buf *b, char c) {
    if (b->len >= UPRINTF_BUF_SIZE) {
        uprintf_flush(b);
    }

    b->data[b->len++] = c;
}

static inline void uprintf_puts_raw(struct uprintf_buf *b, const char *s) {
    if (s == 0) {
        s = "(null)";
    }

    while (*s) {
        uprintf_putc(b, *s);
        s++;
    }
}

/*
 * 无符号十进制。用临时栈缓冲倒序生成，再正序写出。
 * 20 位足够容纳 64 位无符号最大值（18446744073709551615，20 位）。
 */
static inline void uprintf_u64_dec(struct uprintf_buf *b, unsigned long long v) {
    char tmp[20];
    int n = 0;

    if (v == 0) {
        uprintf_putc(b, '0');
        return;
    }

    while (v > 0 && n < (int)sizeof(tmp)) {
        tmp[n++] = (char)('0' + (int)(v % 10));
        v /= 10;
    }

    while (n > 0) {
        uprintf_putc(b, tmp[--n]);
    }
}

static inline void uprintf_i64_dec(struct uprintf_buf *b, long long v) {
    if (v < 0) {
        uprintf_putc(b, '-');
        /* 转成无符号再取负，避免 LLONG_MIN 取负溢出。 */
        uprintf_u64_dec(b, (unsigned long long)(-(v + 1)) + 1ULL);
    } else {
        uprintf_u64_dec(b, (unsigned long long)v);
    }
}

static inline void uprintf_u64_hex(struct uprintf_buf *b, unsigned long long v) {
    static const char digits[] = "0123456789abcdef";
    char tmp[16];   /* 64 位 = 最多 16 个十六进制位 */
    int n = 0;

    if (v == 0) {
        uprintf_putc(b, '0');
        return;
    }

    while (v > 0 && n < (int)sizeof(tmp)) {
        tmp[n++] = digits[(int)(v & 0xf)];
        v >>= 4;
    }

    while (n > 0) {
        uprintf_putc(b, tmp[--n]);
    }
}

static inline void uvprintf(const char *fmt, va_list ap) {
    struct uprintf_buf b;
    b.len = 0;

    while (*fmt) {
        char c = *fmt++;

        if (c != '%') {
            uprintf_putc(&b, c);
            continue;
        }

        /* 读取一个长度修饰符 'l'（支持 %ld / %lu / %lx）。 */
        int is_long = 0;

        if (*fmt == 'l') {
            is_long = 1;
            fmt++;
        }

        char spec = *fmt;

        if (spec == 0) {
            /* 格式串以孤立的 '%' 结尾：原样输出。 */
            uprintf_putc(&b, '%');
            break;
        }

        fmt++;

        switch (spec) {
            case 'd': {
                if (is_long) {
                    long v = va_arg(ap, long);
                    uprintf_i64_dec(&b, (long long)v);
                } else {
                    int v = va_arg(ap, int);
                    uprintf_i64_dec(&b, (long long)v);
                }
                break;
            }

            case 'u': {
                if (is_long) {
                    unsigned long v = va_arg(ap, unsigned long);
                    uprintf_u64_dec(&b, (unsigned long long)v);
                } else {
                    unsigned int v = va_arg(ap, unsigned int);
                    uprintf_u64_dec(&b, (unsigned long long)v);
                }
                break;
            }

            case 'x': {
                if (is_long) {
                    unsigned long v = va_arg(ap, unsigned long);
                    uprintf_u64_hex(&b, (unsigned long long)v);
                } else {
                    unsigned int v = va_arg(ap, unsigned int);
                    uprintf_u64_hex(&b, (unsigned long long)v);
                }
                break;
            }

            case 'p': {
                void *v = va_arg(ap, void *);
                uprintf_putc(&b, '0');
                uprintf_putc(&b, 'x');
                uprintf_u64_hex(&b, (unsigned long long)(usize)v);
                break;
            }

            case 'c': {
                /* char 在可变参数里被提升为 int。 */
                int v = va_arg(ap, int);
                uprintf_putc(&b, (char)v);
                break;
            }

            case 's': {
                const char *v = va_arg(ap, const char *);
                uprintf_puts_raw(&b, v);
                break;
            }

            case '%': {
                uprintf_putc(&b, '%');
                break;
            }

            default: {
                /* 未知格式：原样回显，方便发现写错的格式串。 */
                uprintf_putc(&b, '%');
                if (is_long) {
                    uprintf_putc(&b, 'l');
                }
                uprintf_putc(&b, spec);
                break;
            }
        }
    }

    uprintf_flush(&b);
}

static inline void uprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    uvprintf(fmt, ap);
    va_end(ap);
}

#endif /* UPRINTF_H */