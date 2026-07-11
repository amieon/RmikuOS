// ========== uprintf 实现 ==========
#include <stdarg.h>
#include "syscall.h"
#include "types.h"

#ifndef UPRINTF_BUF_SIZE
#define UPRINTF_BUF_SIZE 1024
#endif

struct uprintf_buf {
    char data[UPRINTF_BUF_SIZE];
    int  len;
};

static inline void uprintf_flush(struct uprintf_buf *b) {
    if (b->len > 0) {
        syscall3(SYS_WRITE, 1, (unsigned long)b->data, (unsigned long)b->len);
        b->len = 0;
    }
}

static inline void uprintf_putc(struct uprintf_buf *b, char ch) {
    if (b->len >= UPRINTF_BUF_SIZE) uprintf_flush(b);
    b->data[b->len++] = ch;
}

static inline void uprintf_puts_raw(struct uprintf_buf *b, const char *s) {
    if (s == 0) s = "(null)";
    while (*s) uprintf_putc(b, *s++);
}

static inline void uprintf_u64_dec(struct uprintf_buf *b, unsigned long long v) {
    char tmp[20]; int n = 0;
    if (v == 0) { uprintf_putc(b, '0'); return; }
    while (v > 0 && n < (int)sizeof(tmp)) { tmp[n++] = (char)('0' + (int)(v % 10)); v /= 10; }
    while (n > 0) uprintf_putc(b, tmp[--n]);
}

static inline void uprintf_i64_dec(struct uprintf_buf *b, long long v) {
    if (v < 0) {
        uprintf_putc(b, '-');
        uprintf_u64_dec(b, (unsigned long long)(-(v + 1)) + 1ULL);
    } else {
        uprintf_u64_dec(b, (unsigned long long)v);
    }
}

static inline void uprintf_u64_hex(struct uprintf_buf *b, unsigned long long v) {
    static const char digits[] = "0123456789abcdef";
    char tmp[16]; int n = 0;
    if (v == 0) { uprintf_putc(b, '0'); return; }
    while (v > 0 && n < (int)sizeof(tmp)) { tmp[n++] = digits[(int)(v & 0xf)]; v >>= 4; }
    while (n > 0) uprintf_putc(b, tmp[--n]);
}

// 辅助：打印 double，prec 位小数
static inline void uprintf_float(struct uprintf_buf *b, double v, int prec) {
    if (v < 0) { uprintf_putc(b, '-'); v = -v; }
    unsigned long long ip = (unsigned long long)v;
    double frac = v - (double)ip;
    if (ip == 0) uprintf_putc(b, '0');
    else {
        char tmp[32]; int n = 0;
        while (ip > 0) { tmp[n++] = '0' + (ip % 10); ip /= 10; }
        while (n > 0) uprintf_putc(b, tmp[--n]);
    }
    uprintf_putc(b, '.');
    for (int i = 0; i < prec; i++) {
        frac *= 10;
        int digit = (int)frac;
        if (digit > 9) digit = 9;
        uprintf_putc(b, '0' + digit);
        frac -= digit;
    }
}
static inline void uprintf_scientific(struct uprintf_buf *b, double v, int prec) {
    if (v < 0) { uprintf_putc(b, '-'); v = -v; }
    if (v == 0.0) {
        uprintf_putc(b, '0'); uprintf_putc(b, '.');
        for (int i = 0; i < prec; i++) uprintf_putc(b, '0');
        uprintf_putc(b, 'e'); uprintf_putc(b, '+'); uprintf_putc(b, '0'); uprintf_putc(b, '0');
        return;
    }
    int exp10 = 0;
    double m = v;
    while (m >= 10.0) { m /= 10.0; exp10++; }
    while (m < 1.0)   { m *= 10.0; exp10--; }
    int d = (int)m;
    uprintf_putc(b, '0' + d);
    uprintf_putc(b, '.');
    double frac = m - d;
    for (int i = 0; i < prec; i++) {
        frac *= 10;
        int digit = (int)frac;
        if (digit > 9) digit = 9;
        uprintf_putc(b, '0' + digit);
        frac -= digit;
    }
    uprintf_putc(b, 'e');
    if (exp10 >= 0) uprintf_putc(b, '+');
    else { uprintf_putc(b, '-'); exp10 = -exp10; }
    if (exp10 < 10) uprintf_putc(b, '0');
    uprintf_u64_dec(b, (unsigned long long)exp10);
}

static inline void uprintf_pad(struct uprintf_buf *b, int width, int len, char pad_char) {
    for (int i = len; i < width; i++) uprintf_putc(b, pad_char);
}

static inline void uprintf_int(struct uprintf_buf *b, long long v, int width, int prec, char pad_char) {
    char tmp[32]; int n = 0;
    int neg = v < 0;
    unsigned long long uv = neg ? (unsigned long long)(-(v + 1)) + 1ULL : (unsigned long long)v;
    if (uv == 0) tmp[n++] = '0';
    while (uv > 0) { tmp[n++] = '0' + (uv % 10); uv /= 10; }
    int total = n + (neg ? 1 : 0);
    int pad_len = width > total ? width - total : 0;
    // 右对齐：先补空格
    for (int i = 0; i < pad_len && pad_char == ' '; i++) uprintf_putc(b, ' ');
    if (neg) uprintf_putc(b, '-');
    while (n > 0) uprintf_putc(b, tmp[--n]);
}

static inline void uvprintf(const char *fmt, va_list ap) {
    struct uprintf_buf b; b.len = 0;
    while (*fmt) {
        char ch = *fmt++;
        if (ch != '%') { uprintf_putc(&b, ch); continue; }

        // 解析格式：%[flags][width][.precision][length]specifier
        int width = 0;
        int prec = -1;  // -1 表示未指定
        int is_long = 0;
        char pad_char = ' ';

        // flags
        if (*fmt == '0') { pad_char = '0'; fmt++; }
        else if (*fmt == '-') { fmt++; }  // 左对齐，暂不实现

        // width
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        // precision
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            while (*fmt >= '0' && *fmt <= '9') {
                prec = prec * 10 + (*fmt - '0');
                fmt++;
            }
        }

        // length
        if (*fmt == 'l') { is_long = 1; fmt++; }


        // 解析 .N 精度
        prec = 6;
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            while (*fmt >= '0' && *fmt <= '9') {
                prec = prec * 10 + (*fmt - '0');
                fmt++;
            }
        }


        char spec = *fmt;
        if (spec == 0) { uprintf_putc(&b, '%'); break; }
        fmt++;

        switch (spec) {
            case 'd': {
                long long v;
                if (is_long) v = va_arg(ap, long);
                else v = va_arg(ap, int);
                uprintf_int(&b, v, width, prec, pad_char);
                break;
            }
            case 'u': {
                unsigned long long v;
                if (is_long) v = va_arg(ap, unsigned long);
                else v = va_arg(ap, unsigned int);
                uprintf_u64_dec(&b, v);  // 暂不支持宽度
                break;
            }
            case 'x': {
                unsigned long long v;
                if (is_long) v = va_arg(ap, unsigned long);
                else v = va_arg(ap, unsigned int);
                uprintf_u64_hex(&b, v);
                break;
            }
            case 'p': {
                void *v = va_arg(ap, void *);
                uprintf_putc(&b, '0'); uprintf_putc(&b, 'x');
                uprintf_u64_hex(&b, (unsigned long long)(usize)v);
                break;
            }
            case 'c': { int v = va_arg(ap, int); uprintf_putc(&b, (char)v); break; }
            case 's': { 
                const char *v = va_arg(ap, const char *); 
                int len = 0; while (v[len]) len++;
                uprintf_pad(&b, width, len, ' ');
                uprintf_puts_raw(&b, v); 
                break; 
            }
            case 'f': {
                double v = va_arg(ap, double);
                uprintf_float(&b, v, prec);
                break;
            }
            case 'e': {
                double v = va_arg(ap, double);
                uprintf_scientific(&b, v, prec);
                break;
            }
            case 'g': {
                double v = va_arg(ap, double);
                double av = v < 0 ? -v : v;
                // %g 规则：指数 < -4 或 >= prec 时用 %e，否则 %f
                if (av == 0.0 || (av >= 1e-4 && av < 1e6)) {
                    uprintf_float(&b, v, prec);
                } else {
                    uprintf_scientific(&b, v, prec);
                }
                break;
            }
            case '%': { uprintf_putc(&b, '%'); break; }
            default: {
                uprintf_putc(&b, '%'); 
                if (is_long) uprintf_putc(&b, 'l'); 
                uprintf_putc(&b, spec);
                break;
            }
        }
    }
    uprintf_flush(&b);
}

static inline void uprintf(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); uvprintf(fmt, ap); va_end(ap);
}

// C 风格 printf 桥接
inline int printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); uvprintf(fmt, ap); va_end(ap);
    return 0;
}
inline int fprintf(int fd, const char* fmt, ...) {
    (void)fd;
    va_list ap; va_start(ap, fmt); uvprintf(fmt, ap); va_end(ap);
    return 0;
}