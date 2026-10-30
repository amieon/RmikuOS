#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include "file.h"   /* 带入 io.h → syscall.h, flag.h; 以及 FILE, stdin/stdout/stderr,
                       fopen, fclose, fgetc, fputc, fread, fwrite, fflush, fputs 等 */
#include "mem.h"    /* getdelim/getline 要 malloc/realloc —— 缓冲长度由库决定,
                       调用者无法预知行长 */

/* C89 <stdio.h> standard macro: max length of a filename string.
 * shell.c uses it for output buffers (char outfile[FILENAME_MAX]). */
#ifndef FILENAME_MAX
#define FILENAME_MAX 512
#endif

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

    while (*fmt) {
        char ch = *fmt++;
        if (ch != '%') { _PUT(ch); continue; }

        /* flags */
        char pad = ' ';
        int alt = 0, left = 0;
        while (*fmt == '0' || *fmt == '-' || *fmt == '#' ||
               *fmt == ' ' || *fmt == '+') {
            if (*fmt == '0') pad = '0';
            if (*fmt == '-') left = 1;
            if (*fmt == '#') alt = 1;
            fmt++;
        }

        /* width: 数字, 或 '*' 从参数取(负数 = 左对齐 + 取绝对值)。
         * git 里 "%-*s" / "%*s" 到处是(utf8.c 对齐、parse-options 缩进),
         * 不支持的话 '*' 会被当成未知转换符原样吐出, 而且**不消费那个 int**,
         * 后面的 %s 就会把长度当成指针解引用 —— 段错误的经典来源。 */
        int width = 0;
        if (*fmt == '*') {
            fmt++;
            width = va_arg(ap, int);
            if (width < 0) { left = 1; width = -width; }
        } else {
            while (*fmt >= '0' && *fmt <= '9')
                width = width * 10 + (*fmt++ - '0');
        }

        /* precision: '.' 后可跟数字或 '*'。负精度按"未指定"处理(C 标准)。 */
        int prec = -1;
        if (*fmt == '.') {
            fmt++;
            if (*fmt == '*') {
                fmt++;
                prec = va_arg(ap, int);
                if (prec < 0) prec = -1;
            } else {
                prec = 0;
                while (*fmt >= '0' && *fmt <= '9')
                    prec = prec * 10 + (*fmt++ - '0');
            }
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

            int neg = v < 0;
            unsigned long long uv = neg ? (unsigned long long)(-(v+1))+1ULL
                                        : (unsigned long long)v;
            char tmp[24]; int n = 0;
            if (uv == 0) tmp[n++] = '0';
            while (uv) { tmp[n++] = (char)('0' + uv%10); uv /= 10; }
            int total = n + neg;
            if (pad=='0' && neg && !left) { _PUT('-'); neg = 0; }
            if (!left) while (total < width) { _PUT(pad); total++; }
            if (neg) _PUT('-');
            while (n > 0) _PUT(tmp[--n]);
            if (left) while (total < width) { _PUT(' '); total++; }
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
            int total = n;
            if (!left) while (total < width) { _PUT(pad); total++; }
            while (n > 0) _PUT(tmp[--n]);
            if (left) while (total < width) { _PUT(' '); total++; }
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
            /* 有精度就只打前 prec 字节。这是 "%.*s" 的语义 —— git 用它打印
             * 非 NUL 结尾的子串(advice.c 逐行加 "hint: " 就是 np - cp)。 */
            int len = 0;
            while (s[len] && (prec < 0 || len < prec)) len++;
            if (!left) for (int i = len; i < width; i++) _PUT(' ');
            for (int i = 0; i < len; i++) _PUT(s[i]);
            if (left) for (int i = len; i < width; i++) _PUT(' ');
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
        int alt = 0, width = 0, left = 0, is_l = 0, is_ll = 0, is_z = 0;
        while (*fmt=='0'||*fmt=='-'||*fmt=='#'||*fmt==' '||*fmt=='+') {
            if (*fmt=='0') pad='0';
            if (*fmt=='-') left=1;
            if (*fmt=='#') alt=1;
            fmt++;
        }
        /* width: 同 vfprintf —— '*' 从参数取, 负数 = 左对齐 */
        if (*fmt=='*') {
            fmt++;
            width = va_arg(ap, int);
            if (width < 0) { left = 1; width = -width; }
        } else {
            while (*fmt>='0'&&*fmt<='9') { width=width*10+(*fmt++-'0'); }
        }
        int prec = -1;
        if (*fmt=='.') {
            fmt++;
            if (*fmt=='*') {
                fmt++;
                prec = va_arg(ap, int);
                if (prec < 0) prec = -1;
            } else {
                prec=0; while(*fmt>='0'&&*fmt<='9') prec=prec*10+(*fmt++-'0');
            }
        }
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
            if (pad=='0'&&neg&&!left) { _SP('-'); neg=0; }
            if (!left) while (total<width) { _SP(pad); total++; }
            if (neg) _SP('-');
            while (n>0) _SP(tmp[--n]);
            if (left) while (total<width) { _SP(' '); total++; }
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
            int total = n;
            if (!left) while (total<width) { _SP(pad); total++; }
            while (n>0) _SP(tmp[--n]);
            if (left) while (total<width) { _SP(' '); total++; }
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
            int len=0;
            while (s[len] && (prec < 0 || len < prec)) len++;
            if (!left) for (int i=len;i<width;i++) _SP(' ');
            for (int i=0;i<len;i++) _SP(s[i]);
            if (left) for (int i=len;i<width;i++) _SP(' ');
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

/* ---- 缓冲模式（setvbuf 用；本实现 stderr 本就无缓冲, 其余可换模式） ---- */
#ifndef _IONBF
#define _IONBF 0
#define _IOLBF 1
#define _IOFBF 2
#endif

static inline int setvbuf(FILE *fp, char *buf, int mode, size_t size) {
    (void)buf; (void)size;
    if (!fp) return EOF;
    if (mode == _IONBF) fp->flags |= _F_UNBUF;
    else                fp->flags &= ~_F_UNBUF;
    return 0;
}

/* setbuf 是 setvbuf 的简化版（POSIX）：buf==NULL 关缓冲，否则用调用方的缓冲。
 * 我们的 FILE 自带 buf[BUFSIZ]，没法接管外部缓冲，所以 buf!=NULL 时只切全缓冲
 * 语义、**不**使用那块内存 —— 可见行为一致，但不能指望写进你的 buf。
 * 实际调用几乎都是 setbuf(fp, NULL)（OpenSSL 的 unbuffer() 就是），这条等价。 */
static inline void setbuf(FILE *fp, char *buf) {
    if (!fp) return;
    if (buf) setvbuf(fp, buf, _IOFBF, BUFSIZ);
    else     setvbuf(fp, (char*)0, _IONBF, 0);
}

static inline int fileno(FILE *fp) {
    return fp ? fp->fd : -1;
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

static inline isize getdelim(char **lineptr, size_t *n, int delim, FILE *fp) {
    if (!lineptr || !n || !fp) { errno = EINVAL; return -1; }

    if (*lineptr == NULL || *n == 0) {
        char *fresh = (char*)malloc(128);
        if (!fresh) { errno = ENOMEM; return -1; }
        *lineptr = fresh;
        *n = 128;
    }

    size_t len = 0;
    for (;;) {
        int ch = fgetc(fp);
        if (ch == EOF) {
            /* 读到一半遇 EOF 是**正常结束**（最后一行没有换行符），
             * 只有"一个字节都没读到"才算失败。 */
            if (len == 0) return -1;
            break;
        }
        /* 留一格给 '\0'：len+1 >= *n 时扩，别等到 len == *n 才动手。
         * 这里不用 realloc 而是 malloc+拷贝+free：realloc 定义在 stdlib.h，
         * 而 stdlib.h 是 include 本文件的 —— 反过来 include 会成环，且环里
         * 的 stdlib 半定义状态用不了它的 inline。扩容代价是行级的，无所谓。 */
        if (len + 1 >= *n) {
            size_t want = *n * 2;
            char *grown = (char*)malloc(want);
            if (!grown) { errno = ENOMEM; return -1; }
            for (size_t i = 0; i < len; i++) grown[i] = (*lineptr)[i];
            free(*lineptr);
            *lineptr = grown;
            *n = want;
        }
        (*lineptr)[len++] = (char)ch;
        if (ch == delim) break;
    }

    (*lineptr)[len] = '\0';
    return (isize)len;
}

static inline isize getline(char **lineptr, size_t *n, FILE *fp) {
    return getdelim(lineptr, n, '\n', fp);
}


typedef struct {
    const char *s;   /* 非 NULL：字符串源，s 自己往前走 */
    FILE *fp;        /* s 为 NULL：流源 */
} rk_scanf_src_t;

static inline int rk_scanf_get(rk_scanf_src_t *src) {
    if (src->s) {
        unsigned char c = (unsigned char)*src->s;
        if (!c) return -1;
        src->s++;
        return (int)c;
    }
    return fgetc(src->fp);
}

/* 至多回退一个字符：解析器每次只多看一个字节 */
static inline void rk_scanf_unget(rk_scanf_src_t *src, int c) {
    if (c < 0) return;
    if (src->s) src->s--;
    else        ungetc(c, src->fp);
}

static inline int rk_scanf_space(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

static inline int rk_scanf_digit(int c, int base) {
    int d;
    if      (c >= '0' && c <= '9') d = c - '0';
    else if (c >= 'a' && c <= 'z') d = c - 'a' + 10;
    else if (c >= 'A' && c <= 'Z') d = c - 'A' + 10;
    else return -1;
    return d < base ? d : -1;
}

/* 按长度修饰写回。LP64 下 long/ll/j/z/t 全是 64 位，共用一条路径。 */
static inline void rk_scanf_store(va_list *ap, int len, int uns, int neg,
                                  unsigned long long v) {
    /* 无符号转换带负号时按模运算回绕：%u 读 "-1" 得 UINT_MAX（不是 1） */
    unsigned long long uv = neg ? (0ULL - v) : v;
    long long sv = neg ? -(long long)v : (long long)v;
    switch (len) {
    case 1:
        if (uns) *va_arg(*ap, unsigned char *) = (unsigned char)uv;
        else     *va_arg(*ap, signed char *)   = (signed char)sv;
        break;
    case 2:
        if (uns) *va_arg(*ap, unsigned short *) = (unsigned short)uv;
        else     *va_arg(*ap, short *)          = (short)sv;
        break;
    case 3:
        if (uns) *va_arg(*ap, unsigned long *) = (unsigned long)uv;
        else     *va_arg(*ap, long *)          = (long)sv;
        break;
    case 4:
    case 5: case 6: case 7:   /* intmax / size / ptrdiff：LP64 下同为 64 位 */
        if (uns) *va_arg(*ap, unsigned long long *) = uv;
        else     *va_arg(*ap, long long *)          = sv;
        break;
    default:
        if (uns) *va_arg(*ap, unsigned int *) = (unsigned int)uv;
        else     *va_arg(*ap, int *)          = (int)sv;
        break;
    }
}

/* 读一个整数。1 = 成功；0 = 匹配失败（不是数字）；-1 = 输入耗尽。
 * base==0（%i）按 C 规则推断：0x→16、0…→8、其余 →10。 */
static inline int rk_scanf_uint(rk_scanf_src_t *src, int base, int width,
                                int *negp, unsigned long long *valp) {
    int c, nd = 0, neg = 0, used = 0;
    unsigned long long v = 0;

    do { c = rk_scanf_get(src); } while (rk_scanf_space(c));
    if (c < 0) return -1;

    if (c == '-' || c == '+') { neg = (c == '-'); used = 1; c = rk_scanf_get(src); }

    if ((base == 16 || base == 0) && c == '0') {
        int c2 = rk_scanf_get(src);
        if (c2 == 'x' || c2 == 'X') {
            base = 16;                  /* 前缀成立，后面还得真有十六进制位 */
            c = rk_scanf_get(src);
            used += 2;
        } else {
            v = 0; nd = 1; used += 1;   /* 那个 '0' 本身就是数字 */
            if (base == 0) base = 8;
            c = c2;
        }
    } else if (base == 0) {
        base = 10;
    }

    while (c >= 0 && (width == 0 || used < width)) {
        int d = rk_scanf_digit(c, base);
        if (d < 0) break;
        v = v * (unsigned)base + (unsigned)d;
        nd++; used++;
        c = rk_scanf_get(src);
    }
    rk_scanf_unget(src, c);
    if (nd == 0) return 0;
    *negp = neg;
    *valp = v;
    return 1;
}

/* %s：先跳空白，再吃到空白为止。dst 为 NULL 表示只消耗不写入（* 抑制符）。
 * 返回 1 成功 / -1 输入耗尽。 */
static inline int rk_scanf_nonspace(rk_scanf_src_t *src, int width, char *dst) {
    int c, n = 0;
    do { c = rk_scanf_get(src); } while (rk_scanf_space(c));
    if (c < 0) return -1;
    while (c >= 0 && !rk_scanf_space(c) && (width == 0 || n < width)) {
        if (dst) dst[n] = (char)c;
        n++;
        c = rk_scanf_get(src);
    }
    rk_scanf_unget(src, c);
    if (dst) dst[n] = '\0';
    return 1;
}

/* %c：不跳空白，不补 '\0'，宽度默认 1。返回 1 满额 / 0 不足 / -1 一个都没读到 */
static inline int rk_scanf_chars(rk_scanf_src_t *src, int width, char *dst) {
    int want = width ? width : 1, n = 0;
    while (n < want) {
        int c = rk_scanf_get(src);
        if (c < 0) break;
        if (dst) dst[n] = (char)c;
        n++;
    }
    if (n == want) return 1;
    return n == 0 ? -1 : 0;
}

static inline void rk_scanf_set_add(unsigned char *set, int c) {
    unsigned char u = (unsigned char)c;
    set[u >> 3] |= (unsigned char)(1u << (u & 7));
}

static inline int rk_scanf_set_has(const unsigned char *set, int c) {
    unsigned char u = (unsigned char)c;
    return (set[u >> 3] >> (u & 7)) & 1;
}

static inline int rk_scanf_vscan(rk_scanf_src_t *src, const char *fmt, va_list ap) {
    int count = 0, eof = 0;

    while (*fmt) {
        if (rk_scanf_space((unsigned char)*fmt)) {
            /* 格式串里的空白 = 吃掉输入里任意多（含 0）个空白 */
            while (rk_scanf_space((unsigned char)*fmt)) fmt++;
            int c;
            do { c = rk_scanf_get(src); } while (rk_scanf_space(c));
            rk_scanf_unget(src, c);
            if (c < 0) eof = 1;
            continue;
        }

        if (*fmt != '%') {
            int c = rk_scanf_get(src);
            if (c != (int)(unsigned char)*fmt) {
                rk_scanf_unget(src, c);
                if (c < 0) eof = 1;
                break;
            }
            fmt++;
            continue;
        }

        fmt++;                            /* 跳过 '%' */
        if (*fmt == '%') {
            int c = rk_scanf_get(src);
            if (c != '%') {
                rk_scanf_unget(src, c);
                if (c < 0) eof = 1;
                break;
            }
            fmt++;
            continue;
        }

        int suppress = 0, width = 0, len = 0;
        if (*fmt == '*') { suppress = 1; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        if      (fmt[0] == 'h' && fmt[1] == 'h') { len = 1; fmt += 2; }
        else if (fmt[0] == 'h')                  { len = 2; fmt++; }
        else if (fmt[0] == 'l' && fmt[1] == 'l') { len = 4; fmt += 2; }
        else if (fmt[0] == 'l')                  { len = 3; fmt++; }
        else if (fmt[0] == 'j')                  { len = 5; fmt++; }
        else if (fmt[0] == 'z')                  { len = 6; fmt++; }
        else if (fmt[0] == 't')                  { len = 7; fmt++; }
        else if (fmt[0] == 'L')                  { len = 8; fmt++; }

        int conv = (unsigned char)*fmt;
        if (!conv) break;                 /* 格式串以 '%' 结尾 */
        fmt++;

        int base = -1, uns = 0;
        if      (conv == 'd')                { base = 10; }
        else if (conv == 'i')                { base = 0;  }
        else if (conv == 'u')                { base = 10; uns = 1; }
        else if (conv == 'o')                { base = 8;  uns = 1; }
        else if (conv == 'x' || conv == 'X') { base = 16; uns = 1; }
        else if (conv == 'p')                { base = 16; uns = 1; len = 4; }

        if (base >= 0) {
            int neg = 0, r;
            unsigned long long v = 0;
            r = rk_scanf_uint(src, base, width, &neg, &v);
            if (r < 0) { eof = 1; break; }   /* 输入耗尽 */
            if (r == 0) break;               /* 匹配失败，不是 EOF */
            if (suppress) continue;
            rk_scanf_store(&ap, len, uns, neg, v);
            count++;
            continue;
        }

        if (conv == 's') {
            char *dst = 0;
            int r;
            if (!suppress) dst = va_arg(ap, char *);
            r = rk_scanf_nonspace(src, width, dst);
            if (r < 0) { eof = 1; break; }
            if (!suppress) count++;
            continue;
        }

        if (conv == 'c') {
            char *dst = 0;
            int r;
            if (!suppress) dst = va_arg(ap, char *);
            r = rk_scanf_chars(src, width, dst);
            if (r < 0) { eof = 1; break; }
            if (r == 0) break;
            if (!suppress) count++;
            continue;
        }

        if (conv == '[') {
            unsigned char set[32] = {0};
            int negate = 0;
            if (*fmt == '^') { negate = 1; fmt++; }
            if (*fmt == ']') { rk_scanf_set_add(set, ']'); fmt++; }   /* 首位 ']' 是字面量 */
            while (*fmt && *fmt != ']') {
                if (fmt[1] == '-' && fmt[2] && fmt[2] != ']') {
                    int a = (unsigned char)fmt[0], b = (unsigned char)fmt[2], ch;
                    for (ch = a; ch <= b; ch++) rk_scanf_set_add(set, ch);
                    fmt += 3;
                } else {
                    rk_scanf_set_add(set, (unsigned char)*fmt);
                    fmt++;
                }
            }
            if (*fmt == ']') fmt++;

            char *dst = 0;
            if (!suppress) dst = va_arg(ap, char *);
            int n = 0, c;
            for (;;) {
                c = rk_scanf_get(src);
                if (c < 0) break;
                if (width && n >= width) break;
                if ((rk_scanf_set_has(set, c) != 0) == negate) break;
                if (dst) dst[n] = (char)c;
                n++;
            }
            rk_scanf_unget(src, c);
            if (dst) dst[n] = '\0';
            if (n == 0) { eof = (c < 0); break; }
            count++;
            continue;
        }

        break;                            /* 不支持的转换：停下，不写错值 */
    }

    return (count == 0 && eof) ? -1 : count;
}

static inline int vsscanf(const char *s, const char *fmt, va_list ap) {
    rk_scanf_src_t src;
    src.s = s;
    src.fp = 0;
    return rk_scanf_vscan(&src, fmt, ap);
}

static inline int vfscanf(FILE *fp, const char *fmt, va_list ap) {
    rk_scanf_src_t src;
    src.s = 0;
    src.fp = fp;
    return rk_scanf_vscan(&src, fmt, ap);
}

static inline int sscanf(const char *s, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsscanf(s, fmt, ap);
    va_end(ap);
    return r;
}

static inline int fscanf(FILE *fp, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vfscanf(fp, fmt, ap);
    va_end(ap);
    return r;
}

static inline int scanf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vfscanf(stdin, fmt, ap);
    va_end(ap);
    return r;
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