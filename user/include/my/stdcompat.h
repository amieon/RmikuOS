#pragma once

#include "compat.h"
#include "vector.h"
#include "cmath.h"
#include "random.h"
#include "string.h"
#include "io.h"

using usize = unsigned long;

// ========== uprintf 实现 ==========
#include <stdarg.h>

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

// ========== std:: 模板元编程工具 ==========
namespace std {
    template<typename T, T v> struct integral_constant {
        static constexpr T value = v;
        using value_type = T;
        using type = integral_constant;
        constexpr operator value_type() const noexcept { return value; }
    };
    using true_type  = integral_constant<bool, true>;
    using false_type = integral_constant<bool, false>;

    template<bool B, typename T, typename F> struct conditional { using type = T; };
    template<typename T, typename F> struct conditional<false, T, F> { using type = F; };

    template<typename T, typename U> struct is_same : false_type {};
    template<typename T> struct is_same<T, T> : true_type {};

    template<typename T> struct remove_reference { using type = T; };
    template<typename T> struct remove_reference<T&> { using type = T; };
    template<typename T> struct remove_reference<T&&> { using type = T; };

    template<typename T> struct remove_cv { using type = T; };
    template<typename T> struct remove_cv<const T> { using type = T; };
    template<typename T> struct remove_cv<volatile T> { using type = T; };
    template<typename T> struct remove_cv<const volatile T> { using type = T; };

    template<typename T> struct is_floating_point : false_type {};
    template<> struct is_floating_point<float> : true_type {};
    template<> struct is_floating_point<double> : true_type {};
}

// ========== std::vector / pair / tuple 别名 ==========
namespace std {
    template<typename T> using vector = mv::Vector<T>;

    template<typename T1, typename T2> using pair = mv::Pair<T1, T2>;
    template<typename T1, typename T2>
    pair<T1, T2> make_pair(T1 a, T2 b) { return pair<T1, T2>(a, b); }

    template<typename T1, typename T2, typename T3> using tuple = mv::Tuple3<T1, T2, T3>;
    template<typename T1, typename T2, typename T3>
    tuple<T1, T2, T3> make_tuple(T1 a, T2 b, T3 c) { return tuple<T1, T2, T3>(a, b, c); }
}

// ========== std::string ==========
namespace std {
    class string {
        mv::Vector<char> buf;
    public:
        string() { buf.push_back('\0'); }
        string(const char* s) {
            if (s) { size_t n = mystr::strlen(s); for (size_t i = 0; i < n; i++) buf.push_back(s[i]); }
            buf.push_back('\0');
        }
        string(const string& o) : buf(o.buf) {}
        string(string&& o) : buf(mv::move(o.buf)) {}
        string& operator=(const char* s) {
            buf.clear(); if (s) { size_t n = mystr::strlen(s); for (size_t i = 0; i < n; i++) buf.push_back(s[i]); }
            buf.push_back('\0'); return *this;
        }
        string& operator=(const string& o) { buf = o.buf; return *this; }
        const char* c_str() const { return buf.data(); }
        size_t size() const { return buf.empty() ? 0 : buf.size() - 1; }
        bool empty() const { return size() == 0; }
        char& operator[](size_t i) { return buf[i]; }
        char operator[](size_t i) const { return buf[i]; }
        char& front() { return buf[0]; }
        char& back() { return buf[size()]; }
        string& operator+=(const string& o) {
            buf.pop_back(); for (size_t i = 0; i < o.size(); i++) buf.push_back(o[i]); buf.push_back('\0'); return *this;
        }
        string& operator+=(const char* s) {
            buf.pop_back(); size_t n = mystr::strlen(s); for (size_t i = 0; i < n; i++) buf.push_back(s[i]); buf.push_back('\0'); return *this;
        }
        string& operator+=(char c) { buf.pop_back(); buf.push_back(c); buf.push_back('\0'); return *this; }
        bool operator==(const string& o) const { return mystr::strcmp(c_str(), o.c_str()) == 0; }
        bool operator!=(const string& o) const { return !(*this == o); }
        bool operator==(const char* s) const { return mystr::strcmp(c_str(), s) == 0; }
        bool operator!=(const char* s) const { return !(*this == s); }
    };
    inline string operator+(const string& a, const string& b) { string r = a; r += b; return r; }
    inline string operator+(const string& a, const char* b) { string r = a; r += b; return r; }
}

// ========== std::ifstream / istringstream ==========
namespace std {
    class ifstream {
        char* buf_;
        size_t sz_;
        char* cur_;
        bool ok_;
    public:
        ifstream() : buf_(nullptr), sz_(0), cur_(nullptr), ok_(false) {}
        explicit ifstream(const char* path) { open(path); }
        explicit ifstream(const string& path) { open(path.c_str()); }
        void open(const char* path) {
            buf_ = mystr::read_file(path, sz_);
            ok_ = (buf_ != nullptr); cur_ = buf_;
        }
        void open(const string& path) { open(path.c_str()); }
        bool is_open() const { return ok_; }
        operator bool() const {  return ok_ && cur_ && (size_t)(cur_ - buf_) < sz_;  }
        bool operator!() const { return !ok_; }
        void close() { if (buf_) { free(buf_); buf_ = nullptr; } ok_ = false; }
        ~ifstream() { close(); }

        bool getline(string& out) {
            out = string();
            if (!ok_ || !cur_ || (size_t)(cur_ - buf_) >= sz_) return false;
            while ((size_t)(cur_ - buf_) < sz_ && *cur_ != '\n') { out += *cur_++; }
            if ((size_t)(cur_ - buf_) < sz_ && *cur_ == '\n') cur_++;
            return true;
        }

        // operator>> for string
        ifstream& operator>>(string& out) {
            out = string();
            if (!ok_ || !cur_ || (size_t)(cur_ - buf_) >= sz_) {
                ok_ = false;  // ← 到达 EOF，标记失败
                return *this;
            }
            // 跳过空白
            while ((size_t)(cur_ - buf_) < sz_ && (*cur_ == ' ' || *cur_ == '\t' || *cur_ == '\r' || *cur_ == '\n')) cur_++;
            if ((size_t)(cur_ - buf_) >= sz_) {
                ok_ = false;  // ← 只有空白，没有实质内容
                return *this;
            }
            // 读取 token
            while ((size_t)(cur_ - buf_) < sz_ && *cur_ != ' ' && *cur_ != '\t' && *cur_ != '\r' && *cur_ != '\n') {
                out += *cur_++;
            }
            return *this;
        }

        // operator>> for int
        ifstream& operator>>(int& out) {
            string s; *this >> s;
            out = mystr::str_to_int(s.c_str());
            return *this;
        }

        // operator>> for double
        ifstream& operator>>(double& out) {
            string s; *this >> s;
            out = mystr::str_to_double(s.c_str());
            return *this;
        }
    };
    inline bool getline(ifstream& f, string& out) { return f.getline(out); }

    class istringstream {
        char* ptr_;
        char* end_;
        bool ok_;  
    public:
        istringstream() : ptr_(nullptr), end_(nullptr), ok_(false) {}
        istringstream(const char* s) { str(s); }
        istringstream(const string& s) { str(s.c_str()); }
        void str(const char* s) { 
            ptr_ = (char*)s; 
            end_ = ptr_ + (s ? mystr::strlen(s) : 0); 
            ok_ = (s != nullptr); 
        }
        void str(const string& s) { str(s.c_str()); }

        istringstream& operator>>(string& out) {
            out = string();
            if (!ok_ || ptr_ >= end_) {
                ok_ = false;
                return *this;
            }
            while (ptr_ < end_ && (*ptr_ == ' ' || *ptr_ == '\t' || *ptr_ == '\r' || *ptr_ == '\n')) ptr_++;
            if (ptr_ >= end_) {
                ok_ = false;
                return *this;
            }
            while (ptr_ < end_ && *ptr_ != ' ' && *ptr_ != '\t' && *ptr_ != '\r' && *ptr_ != '\n') {
                out += *ptr_++;
            }
            return *this;
        }
        operator bool() const { return ok_ && ptr_ < end_; }  // ← 检查 ok_
    };
}

// ========== std::unordered_map（完整模板） ==========
namespace std {
    template<typename K, typename V>
    class unordered_map {
        struct Entry { K first; V second; };
        mv::Vector<Entry> entries;
    public:
        struct iterator {
            Entry* p;
            iterator(Entry* _p = nullptr) : p(_p) {}
            Entry* operator->() { return p; }
            bool operator!=(const iterator& o) const { return p != o.p; }
            bool operator==(const iterator& o) const { return p == o.p; }
        };
        iterator end() { return iterator(nullptr); }
        iterator find(const K& key) {
            for (size_t i = 0; i < entries.size(); i++)
                if (entries[i].first == key) return iterator(&entries[i]);
            return end();
        }
        V& operator[](const K& key) {
            for (size_t i = 0; i < entries.size(); i++)
                if (entries[i].first == key) return entries[i].second;
            entries.push_back(Entry{key, V()});
            return entries.back().second;
        }
        size_t size() const { return entries.size(); }
    };
}

// ========== std::algorithm ==========
namespace std {
    template<typename T> void swap(T& a, T& b) { mv::swap(a, b); }
    template<typename Iter> void sort(Iter first, Iter last) { mv::sort(first, last); }
    template<typename Iter> Iter unique(Iter first, Iter last) { return mv::unique(first, last); }
    template<typename Iter, typename RNG> void shuffle(Iter first, Iter last, RNG& rng) { mymath::shuffle(first, last, rng); }
    template<typename T, typename Iter> void iota(Iter first, Iter last, T val) { mv::iota(first, last, val); }

    template<typename T, typename U> auto max(T a, U b) -> decltype(a > b ? a : b) { return a > b ? a : b; }
    template<typename T, typename U> auto min(T a, U b) -> decltype(a < b ? a : b) { return a < b ? a : b; }
    template<typename T> T abs(T x) { return x < 0 ? -x : x; }
    inline double abs(double x) { return mymath::fabs(x); }
    inline float abs(float x) { return mymath::fabs(x); }
    inline double fabs(double x) { return mymath::fabs(x); }
    inline float fabs(float x) { return mymath::fabs(x); }

    template<typename T> T move(T& x) { return mv::move(x); }
    template<typename T> T move(T&& x) { return mv::move(x); }
}

// ========== std::cmath ==========
namespace std {
    inline double exp(double x) { return mymath::exp(x); }
    inline double log(double x) { return mymath::log(x); }
    inline double sqrt(double x) { return mymath::sqrt(x); }
    inline double pow(double x, double y) { return mymath::pow(x, y); }
    inline double cos(double x) { return mymath::cos(x); }
    inline double sin(double x) { return mymath::sin(x); }
}

// ========== std::cstdlib ==========
namespace std {
    inline double stod(const string& s) { return mystr::str_to_double(s.c_str()); }
    inline int stoi(const string& s) { return mystr::str_to_int(s.c_str()); }
}

// ========== std::random ==========
namespace std {
    using mt19937 = mymath::RNG;
    using default_random_engine = mymath::RNG;
    template<typename T> using uniform_real_distribution = mymath::uniform_real_distribution<T>;
    template<typename T> using normal_distribution = mymath::normal_distribution<T>;
    template<typename T> using uniform_int_distribution = mymath::uniform_int_distribution<T>;
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