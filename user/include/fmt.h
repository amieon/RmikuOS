#pragma once

#include "io.h"
#include <stdarg.h>

/* ---- 解析 ---- */

static int parse_int(const char *s) {
    int x = 0;
    int sign = 1;

    if (*s == '-') {
        sign = -1;
        s++;
    }

    while (*s >= '0' && *s <= '9') {
        x = x * 10 + (*s - '0');
        s++;
    }

    return x * sign;
}

/* ---- 直接输出 ---- */

static inline void put_int(long x) {
    char buf[32];
    int i = 0;

    if (x == 0) {
        put_char('0');
        return;
    }

    if (x < 0) {
        put_char('-');
        x = -x;
    }

    while (x > 0) {
        buf[i++] = '0' + (x % 10);
        x /= 10;
    }

    while (i > 0) {
        i--;
        put_char(buf[i]);
    }
}

static char c(long x) {
    if (x <= 9) return x + '0';
    return x - 10 + 'a';
}

static inline void put_hex(long x) {
    char buf[32];
    int i = 0;

    if (x == 0) {
        put_char('0');
        return;
    }

    if (x < 0) {
        put_char('-');
        x = -x;
    }

    while (x > 0) {
        buf[i++] = c(x % 16);
        x /= 16;
    }

    while (i > 0) {
        i--;
        put_char(buf[i]);
    }
}

/* ---- 缓冲拼接 ---- */

static int append_str(char *buf, int pos, const char *s) {
    while (*s) {
        buf[pos++] = *s++;
    }
    return pos;
}

static int append_int(char *buf, int pos, int x) {
    char tmp[16];
    int n = 0;

    if (x == 0) {
        buf[pos++] = '0';
        return pos;
    }

    if (x < 0) {
        buf[pos++] = '-';
        x = -x;
    }

    while (x > 0) {
        tmp[n++] = '0' + (x % 10);
        x /= 10;
    }

    while (n > 0) {
        buf[pos++] = tmp[--n];
    }

    return pos;
}

static int append_usize(char *buf, int pos, usize x) {
    char tmp[32];
    int n = 0;

    if (x == 0) {
        buf[pos++] = '0';
        return pos;
    }

    while (x > 0) {
        tmp[n++] = '0' + (x % 10);
        x /= 10;
    }

    while (n > 0) {
        buf[pos++] = tmp[--n];
    }

    return pos;
}

/* ---- 比较 ---- */

static int str_eq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}


 /*  uprintf —— 极简用户态 printf
 *
 *  攒满一块栈缓冲再一次性 write,而不是每个字符一次系统调用。
 *
 *  支持:%d %u %ld %lu %x %lx %p %c %s %%
 *  不支持宽度 / 精度 / 补零。
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

static inline void uprintf_putc(struct uprintf_buf *b, char ch) {
    if (b->len >= UPRINTF_BUF_SIZE) {
        uprintf_flush(b);
    }

    b->data[b->len++] = ch;
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
        /* 转成无符号再取负,避免 LLONG_MIN 取负溢出。 */
        uprintf_u64_dec(b, (unsigned long long)(-(v + 1)) + 1ULL);
    } else {
        uprintf_u64_dec(b, (unsigned long long)v);
    }
}

static inline void uprintf_u64_hex(struct uprintf_buf *b, unsigned long long v) {
    static const char digits[] = "0123456789abcdef";
    char tmp[16];
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
        char ch = *fmt++;

        if (ch != '%') {
            uprintf_putc(&b, ch);
            continue;
        }

        int is_long = 0;

        if (*fmt == 'l') {
            is_long = 1;
            fmt++;
        }

        char spec = *fmt;

        if (spec == 0) {
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
