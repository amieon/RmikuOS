#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include "file.h"   /* 带入 io.h → syscall.h, flag.h; 以及 FILE, stdin/stdout/stderr,
                       fopen, fclose, fgetc, fputc, fread, fwrite, fflush, fputs 等 */

/* ================================================================
 *  内部：向 FILE* 逐字符输出（复用 file.h 的缓冲）
 * ================================================================ */

static inline void __pf_putc(FILE *fp, char ch) {
    fputc((unsigned char)ch, fp);
}

static inline void __pf_puts(FILE *fp, const char *s) {
    if (!s) s = "(null)";
    while (*s) fputc((unsigned char)*s++, fp);
}

static inline void __pf_u64(FILE *fp, unsigned long long v, int base, int upper) {
    const char *d = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[64];
    int n = 0;
    if (v == 0) { fputc('0', fp); return; }
    while (v) { tmp[n++] = d[v % base]; v /= base; }
    while (n > 0) fputc((unsigned char)tmp[--n], fp);
}

static inline void __pf_i64(FILE *fp, long long v, int width, char pad) {
    int neg = v < 0;
    unsigned long long uv = neg ? (unsigned long long)(-(v + 1)) + 1ULL
                                : (unsigned long long)v;
    char tmp[24];
    int n = 0;
    if (uv == 0) tmp[n++] = '0';
    while (uv) { tmp[n++] = (char)('0' + uv % 10); uv /= 10; }
    int total = n + neg;
    if (pad == '0' && neg) { fputc('-', fp); neg = 0; }
    while (total < width) { fputc((unsigned char)pad, fp); total++; }
    if (neg) fputc('-', fp);
    while (n > 0) fputc((unsigned char)tmp[--n], fp);
}

static inline void __pf_float(FILE *fp, double v, int prec) {
    if (v < 0) { fputc('-', fp); v = -v; }
    unsigned long long ip = (unsigned long long)v;
    double frac = v - (double)ip;
    __pf_u64(fp, ip, 10, 0);
    fputc('.', fp);
    for (int i = 0; i < prec; i++) {
        frac *= 10.0;
        int d = (int)frac;
        if (d > 9) d = 9;
        fputc((char)('0' + d), fp);
        frac -= d;
    }
}

static inline void __pf_sci(FILE *fp, double v, int prec) {
    if (v < 0) { fputc('-', fp); v = -v; }
    if (v == 0.0) {
        __pf_puts(fp, "0.");
        for (int i = 0; i < prec; i++) fputc('0', fp);
        __pf_puts(fp, "e+00");
        return;
    }
    int exp10 = 0;
    double m = v;
    while (m >= 10.0) { m /= 10.0; exp10++; }
    while (m < 1.0)   { m *= 10.0; exp10--; }
    int d = (int)m;
    fputc((char)('0' + d), fp);
    fputc('.', fp);
    double frac = m - d;
    for (int i = 0; i < prec; i++) {
        frac *= 10.0;
        int dd = (int)frac;
        if (dd > 9) dd = 9;
        fputc((char)('0' + dd), fp);
        frac -= dd;
    }
    fputc('e', fp);
    fputc(exp10 >= 0 ? '+' : '-', fp);
    if (exp10 < 0) exp10 = -exp10;
    if (exp10 < 10) fputc('0', fp);
    __pf_u64(fp, (unsigned long long)exp10, 10, 0);
}

/* ================================================================
 *  vfprintf —— 格式化核心
 * ================================================================ */

static inline int vfprintf(FILE *fp, const char *fmt, va_list ap) {
    if (!fp) return -1;
    int count = 0;

    /* 包装 fputc 同时计数 */
    #define _PUT(ch) do { fputc((unsigned char)(ch), fp); count++; } while(0)
    #define _PUTS(s) do { const char *_s=(s); if(!_s)_s="(null)"; \
                          while(*_s){ fputc((unsigned char)*_s++,fp); count++; } } while(0)

    while (*fmt) {
        char ch = *fmt++;
        if (ch != '%') { _PUT(ch); continue; }

        /* flags */
        char pad = ' ';
        int alt = 0;
        while (*fmt == '0' || *fmt == '-' || *fmt == '#') {
            if (*fmt == '0') pad = '0';
            if (*fmt == '#') alt = 1;
            fmt++;
        }

        /* width */
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');

        /* precision */
        int prec = -1;
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            while (*fmt >= '0' && *fmt <= '9')
                prec = prec * 10 + (*fmt++ - '0');
        }

        /* length */
        int is_ll = 0, is_l = 0, is_z = 0;
        if (*fmt == 'l') {
            fmt++;
            if (*fmt == 'l') { is_ll = 1; fmt++; }
            else is_l = 1;
        } else if (*fmt == 'z') { is_z = 1; fmt++; }

        char spec = *fmt;
        if (!spec) { _PUT('%'); break; }
        fmt++;

        int fprec = (prec < 0 && (spec=='f'||spec=='F'||spec=='e'||
                     spec=='E'||spec=='g'||spec=='G')) ? 6 : prec;

        switch (spec) {
        case 'd': case 'i': {
            long long v = is_ll ? va_arg(ap, long long)
                        : is_l  ? va_arg(ap, long)
                        : is_z  ? (long long)va_arg(ap, usize)
                        :         va_arg(ap, int);
            /* 手动展开 __pf_i64 以计数 */
            int neg = v < 0;
            unsigned long long uv = neg ? (unsigned long long)(-(v+1))+1ULL
                                        : (unsigned long long)v;
            char tmp[24]; int n = 0;
            if (uv == 0) tmp[n++] = '0';
            while (uv) { tmp[n++] = (char)('0' + uv%10); uv /= 10; }
            int total = n + neg;
            if (pad=='0' && neg) { _PUT('-'); neg = 0; }
            while (total < width) { _PUT(pad); total++; }
            if (neg) _PUT('-');
            while (n > 0) _PUT(tmp[--n]);
            break;
        }
        case 'u': case 'o': case 'x': case 'X': {
            unsigned long long v = is_ll ? va_arg(ap, unsigned long long)
                                 : is_l  ? va_arg(ap, unsigned long)
                                 : is_z  ? (unsigned long long)va_arg(ap, usize)
                                 :         va_arg(ap, unsigned int);
            int base = spec=='u' ? 10 : spec=='o' ? 8 : 16;
            int upper = (spec == 'X');
            if (alt && v) {
                _PUT('0');
                _PUT(upper ? 'X' : 'x');
            }
            const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
            char tmp[64]; int n = 0;
            if (v == 0) tmp[n++] = '0';
            while (v) { tmp[n++] = dig[v % base]; v /= base; }
            while (n < width) { _PUT(pad); width--; }
            while (n > 0) _PUT(tmp[--n]);
            break;
        }
        case 'p': {
            void *v = va_arg(ap, void *);
            _PUT('0'); _PUT('x');
            unsigned long long uv = (unsigned long long)(usize)v;
            const char *dig = "0123456789abcdef";
            char tmp[16]; int n = 0;
            if (uv == 0) tmp[n++] = '0';
            while (uv) { tmp[n++] = dig[uv & 0xf]; uv >>= 4; }
            while (n > 0) _PUT(tmp[--n]);
            break;
        }
        case 'c':
            _PUT((char)va_arg(ap, int));
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            int len = 0; while (s[len]) len++;
            for (int i = len; i < width; i++) _PUT(' ');
            _PUTS(s);
            break;
        }
        case 'f': case 'F': {
            double v = va_arg(ap, double);
            if (v < 0) { _PUT('-'); v = -v; }
            unsigned long long ip = (unsigned long long)v;
            double frac = v - (double)ip;
            /* 整数部分 */
            char tmp[24]; int n = 0;
            if (ip == 0) tmp[n++] = '0';
            while (ip) { tmp[n++] = (char)('0' + ip%10); ip /= 10; }
            while (n > 0) _PUT(tmp[--n]);
            _PUT('.');
            for (int i = 0; i < fprec; i++) {
                frac *= 10.0;
                int d = (int)frac; if (d > 9) d = 9;
                _PUT((char)('0' + d));
                frac -= d;
            }
            break;
        }
        case 'e': case 'E': {
            double v = va_arg(ap, double);
            if (v < 0) { _PUT('-'); v = -v; }
            if (v == 0.0) {
                _PUT('0'); _PUT('.');
                for (int i = 0; i < fprec; i++) _PUT('0');
                _PUT('e'); _PUT('+'); _PUT('0'); _PUT('0');
                break;
            }
            int exp10 = 0;
            double m = v;
            while (m >= 10.0) { m /= 10.0; exp10++; }
            while (m < 1.0)   { m *= 10.0; exp10--; }
            int d = (int)m;
            _PUT((char)('0' + d)); _PUT('.');
            double frac = m - d;
            for (int i = 0; i < fprec; i++) {
                frac *= 10.0;
                int dd = (int)frac; if (dd > 9) dd = 9;
                _PUT((char)('0' + dd));
                frac -= dd;
            }
            _PUT('e');
            _PUT(exp10 >= 0 ? '+' : '-');
            if (exp10 < 0) exp10 = -exp10;
            if (exp10 < 10) _PUT('0');
            /* 指数部分（最多两位以上） */
            char etmp[8]; int en = 0;
            if (exp10 == 0) etmp[en++] = '0';
            while (exp10) { etmp[en++] = (char)('0' + exp10%10); exp10 /= 10; }
            while (en > 0) _PUT(etmp[--en]);
            break;
        }
        case 'g': case 'G': {
            double v = va_arg(ap, double);
            double av = v < 0 ? -v : v;
            /* 简化：用 %f 或 %e 的判定 */
            if (av == 0.0 || (av >= 1e-4 && av < 1e6)) {
                /* 走 %f 路径（重新压回 va_list 不现实，直接内联） */
                if (v < 0) { _PUT('-'); v = -v; }
                unsigned long long ip = (unsigned long long)v;
                double frac = v - (double)ip;
                char tmp[24]; int n = 0;
                if (ip == 0) tmp[n++] = '0';
                while (ip) { tmp[n++] = (char)('0'+ip%10); ip/=10; }
                while (n > 0) _PUT(tmp[--n]);
                _PUT('.');
                for (int i = 0; i < fprec; i++) {
                    frac *= 10.0;
                    int dd = (int)frac; if (dd>9) dd=9;
                    _PUT((char)('0'+dd)); frac -= dd;
                }
            } else {
                /* 走 %e 路径 */
                if (v < 0) { _PUT('-'); v = -v; }
                int exp10 = 0;
                double m = v;
                while (m >= 10.0) { m /= 10.0; exp10++; }
                while (m < 1.0)   { m *= 10.0; exp10--; }
                int d = (int)m;
                _PUT((char)('0'+d)); _PUT('.');
                double frac = m - d;
                for (int i = 0; i < fprec; i++) {
                    frac *= 10.0;
                    int dd = (int)frac; if (dd>9) dd=9;
                    _PUT((char)('0'+dd)); frac -= dd;
                }
                _PUT('e');
                _PUT(exp10>=0?'+':'-');
                if (exp10<0) exp10=-exp10;
                if (exp10<10) _PUT('0');
                char etmp[8]; int en=0;
                if (exp10==0) etmp[en++]='0';
                while (exp10) { etmp[en++]=(char)('0'+exp10%10); exp10/=10; }
                while (en>0) _PUT(etmp[--en]);
            }
            break;
        }
        case '%':
            _PUT('%');
            break;
        default:
            _PUT('%');
            _PUT(spec);
            break;
        }
    }

    #undef _PUT
    #undef _PUTS
    return count;
}

/* ================================================================
 *  vsnprintf —— 写入字符串
 * ================================================================ */

static inline int __sn_put(char *s, int cap, int pos, char ch) {
    if (pos < cap - 1) s[pos] = ch;
    return pos + 1;
}

static inline int vsnprintf(char *str, usize cap, const char *fmt, va_list ap) {
    int pos = 0, c = (int)cap;

    #define _SP(ch) do { pos = __sn_put(str, c, pos, (ch)); } while(0)

    while (*fmt) {
        char ch = *fmt++;
        if (ch != '%') { _SP(ch); continue; }

        char pad = ' ';
        int alt = 0, width = 0, is_l = 0, is_ll = 0, is_z = 0;
        while (*fmt=='0'||*fmt=='-'||*fmt=='#') {
            if (*fmt=='0') pad='0';
            if (*fmt=='#') alt=1;
            fmt++;
        }
        while (*fmt>='0'&&*fmt<='9') { width=width*10+(*fmt++-'0'); }
        int prec = -1;
        if (*fmt=='.') { fmt++; prec=0; while(*fmt>='0'&&*fmt<='9') prec=prec*10+(*fmt++-'0'); }
        if (*fmt=='l') { fmt++; if(*fmt=='l'){is_ll=1;fmt++;} else is_l=1; }
        else if (*fmt=='z') { is_z=1; fmt++; }

        char spec = *fmt ? *fmt++ : 0;
        switch (spec) {
        case 'd': case 'i': {
            long long v = is_ll ? va_arg(ap,long long)
                        : is_l  ? va_arg(ap,long)
                        : is_z  ? (long long)va_arg(ap,usize)
                        :         va_arg(ap,int);
            int neg = v<0;
            unsigned long long uv = neg?(unsigned long long)(-(v+1))+1ULL:(unsigned long long)v;
            char tmp[24]; int n=0;
            if (uv==0) tmp[n++]='0';
            while (uv) { tmp[n++]=(char)('0'+uv%10); uv/=10; }
            int total = n+neg;
            if (pad=='0'&&neg) { _SP('-'); neg=0; }
            while (total<width) { _SP(pad); total++; }
            if (neg) _SP('-');
            while (n>0) _SP(tmp[--n]);
            break;
        }
        case 'u': case 'o': case 'x': case 'X': {
            unsigned long long v = is_ll ? va_arg(ap,unsigned long long)
                                 : is_l  ? va_arg(ap,unsigned long)
                                 : is_z  ? (unsigned long long)va_arg(ap,usize)
                                 :         va_arg(ap,unsigned int);
            int base = spec=='u'?10:spec=='o'?8:16;
            int upper = (spec=='X');
            if (alt&&v) { _SP('0'); _SP(upper?'X':'x'); }
            const char *dig = upper?"0123456789ABCDEF":"0123456789abcdef";
            char tmp[64]; int n=0;
            if (v==0) tmp[n++]='0';
            while (v) { tmp[n++]=dig[v%base]; v/=base; }
            while (n<width) { _SP(pad); width--; }
            while (n>0) _SP(tmp[--n]);
            break;
        }
        case 'p': {
            unsigned long long v = (unsigned long long)(usize)va_arg(ap,void*);
            _SP('0'); _SP('x');
            const char *dig="0123456789abcdef";
            char tmp[16]; int n=0;
            if (v==0) tmp[n++]='0';
            while (v) { tmp[n++]=dig[v&0xf]; v>>=4; }
            while (n>0) _SP(tmp[--n]);
            break;
        }
        case 'c': _SP((char)va_arg(ap,int)); break;
        case 's': {
            const char *s = va_arg(ap,const char*);
            if (!s) s="(null)";
            int len=0; while(s[len]) len++;
            for (int i=len;i<width;i++) _SP(' ');
            while (*s) _SP(*s++);
            break;
        }
        case 'f': case 'F': {
             double v = va_arg(ap, double);
            /* ---- 特殊值检查（避免 infinity 死循环） ---- */
            if (v != v) {  /* NaN（NaN != NaN） */
                const char *_s = (spec=='G'||spec=='F'||spec=='E') ? "NAN" : "nan";
                while (*_s) _SP(*_s++);
                break;
            }
            if (v > 1e308 || v < -1e308) {  /* ±infinity */
                if (v < 0) _SP('-');
                const char *_s = (spec=='G'||spec=='F'||spec=='E') ? "INF" : "inf";
                while (*_s) _SP(*_s++);
                break;
            }
            if (v < 0) { _SP('-'); v = -v; }
            unsigned long long ip = (unsigned long long)v;
            double frac = v - (double)ip;
            char tmp[24]; int n = 0;
            if (ip == 0) tmp[n++] = '0';
            while (ip) { tmp[n++] = (char)('0'+ip%10); ip/=10; }
            while (n > 0) _SP(tmp[--n]);
            _SP('.');
            if (prec < 0) prec = 6;
            for (int i = 0; i < prec; i++) {
                frac *= 10.0;
                int d = (int)frac; if (d>9) d=9;
                _SP((char)('0'+d)); frac -= d;
            }
            break;
            }
        case 'e': case 'E': {
             double v = va_arg(ap, double);
            /* ---- 特殊值检查（避免 infinity 死循环） ---- */
            if (v != v) {  /* NaN（NaN != NaN） */
                const char *_s = (spec=='G'||spec=='F'||spec=='E') ? "NAN" : "nan";
                while (*_s) _SP(*_s++);
                break;
            }
            if (v > 1e308 || v < -1e308) {  /* ±infinity */
                if (v < 0) _SP('-');
                const char *_s = (spec=='G'||spec=='F'||spec=='E') ? "INF" : "inf";
                while (*_s) _SP(*_s++);
                break;
            }
            if (v < 0) { _SP('-'); v = -v; }
            if (v == 0.0) {
                _SP('0'); _SP('.');
                if (prec < 0) prec = 6;
                for (int i = 0; i < prec; i++) _SP('0');
                _SP(spec=='E'?'E':'e'); _SP('+'); _SP('0'); _SP('0');
                break;
            }
            int exp10 = 0; double m = v;
            while (m >= 10.0) { m /= 10.0; exp10++; }
            while (m < 1.0)   { m *= 10.0; exp10--; }
            int d = (int)m;
            _SP((char)('0'+d)); _SP('.');
            if (prec < 0) prec = 6;
            double frac = m - d;
            for (int i = 0; i < prec; i++) {
                frac *= 10.0; int dd = (int)frac; if (dd>9) dd=9;
                _SP((char)('0'+dd)); frac -= dd;
            }
            _SP(spec=='E'?'E':'e'); _SP(exp10>=0?'+':'-');
            if (exp10<0) exp10=-exp10;
            if (exp10<10) _SP('0');
            char et[8]; int en=0;
            if (exp10==0) et[en++]='0';
            while (exp10) { et[en++]=(char)('0'+exp10%10); exp10/=10; }
            while (en>0) _SP(et[--en]);
            break;
        }
        case 'g': case 'G': {
             double v = va_arg(ap, double);
            /* ---- 特殊值检查（避免 infinity 死循环） ---- */
            if (v != v) {  /* NaN（NaN != NaN） */
                const char *_s = (spec=='G'||spec=='F'||spec=='E') ? "NAN" : "nan";
                while (*_s) _SP(*_s++);
                break;
            }
            if (v > 1e308 || v < -1e308) {  /* ±infinity */
                if (v < 0) _SP('-');
                const char *_s = (spec=='G'||spec=='F'||spec=='E') ? "INF" : "inf";
                while (*_s) _SP(*_s++);
                break;
            }
            double av = v < 0 ? -v : v;
            if (prec < 0) prec = 6;
            if (av == 0.0 || (av >= 1e-4 && av < 1e6)) {
                /* 走 %f */
                if (v < 0) { _SP('-'); v = -v; }
                unsigned long long ip = (unsigned long long)v;
                double frac = v - (double)ip;
                char tmp[24]; int n = 0;
                if (ip == 0) tmp[n++] = '0';
                while (ip) { tmp[n++] = (char)('0'+ip%10); ip/=10; }
                while (n > 0) _SP(tmp[--n]);
                _SP('.');
                for (int i = 0; i < prec; i++) {
                    frac *= 10.0; int dd = (int)frac; if (dd>9) dd=9;
                    _SP((char)('0'+dd)); frac -= dd;
                }
            } else {
                /* 走 %e */
                if (v < 0) { _SP('-'); v = -v; }
                int exp10 = 0; double m = v;
                while (m >= 10.0) { m /= 10.0; exp10++; }
                while (m < 1.0)   { m *= 10.0; exp10--; }
                int d = (int)m;
                _SP((char)('0'+d)); _SP('.');
                double frac = m - d;
                for (int i = 0; i < prec; i++) {
                    frac *= 10.0; int dd = (int)frac; if (dd>9) dd=9;
                    _SP((char)('0'+dd)); frac -= dd;
                }
                _SP(spec=='G'?'E':'e'); _SP(exp10>=0?'+':'-');
                if (exp10<0) exp10=-exp10;
                if (exp10<10) _SP('0');
                char et[8]; int en=0;
                if (exp10==0) et[en++]='0';
                while (exp10) { et[en++]=(char)('0'+exp10%10); exp10/=10; }
                while (en>0) _SP(et[--en]);
            }
            break;
        }

        case '%': _SP('%'); break;
        default:  _SP('%'); if(spec) _SP(spec); break;
        }
    }
    #undef _SP

    if (c > 0) str[pos < c ? pos : c-1] = '\0';
    return pos;
}

/* ================================================================
 *  公开 API
 * ================================================================ */

static inline int fprintf(FILE *fp, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vfprintf(fp, fmt, ap);
    va_end(ap);
    return r;
}

static inline int printf(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return r;
}

static inline int vprintf(const char *fmt, va_list ap) {
    return vfprintf(stdout, fmt, ap);
}

static inline int sprintf(char *str, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vsnprintf(str, (usize)0x7fffffff, fmt, ap);
    va_end(ap);
    return r;
}

static inline int snprintf(char *str, usize cap, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vsnprintf(str, cap, fmt, ap);
    va_end(ap);
    return r;
}

/* ================================================================
 *  字符 / 字符串 I/O
 * ================================================================ */

static inline int putchar(int ch) {
    return fputc(ch, stdout);
}

static inline int getchar(void) {
    return fgetc(stdin);
}

/* 标准 puts：输出字符串 + 换行，返回非负值或 EOF */
static inline int puts(const char *s) {
    if (!s) return EOF;
    if (fputs(s, stdout) == EOF) return EOF;
    if (fputc('\n', stdout) == EOF) return EOF;
    return 0;
}

static inline char *fgets(char *s, int size, FILE *fp) {
    if (!s || size <= 0 || !fp) return (char*)0;
    int i = 0;
    while (i < size - 1) {
        int ch = fgetc(fp);
        if (ch == EOF) break;
        s[i++] = (char)ch;
        if (ch == '\n') break;
    }
    s[i] = '\0';
    return i > 0 ? s : (char*)0;
}

/* ================================================================
 *  杂项
 * ================================================================ */

static inline void perror(const char *msg) {
    if (msg && *msg) {
        fputs(msg, stderr);
        fputs(": ", stderr);
    }
    fputs("error\n", stderr);
}

static inline int rename_file(const char *old, const char *new_) {
    /* 裸机简易实现：读旧→写新→删旧，或者你有 syscall 就直接调 */
    (void)old; (void)new_;
    return -1; /* TODO: 实现或删掉 */
}

static inline int atoi(const char *s) {
    int n = 0, sign = 1;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') { s++; }
    while (*s >= '0' && *s <= '9')
        n = n * 10 + (*s++ - '0');
    return sign * n;
}

#ifdef __cplusplus
}
#endif