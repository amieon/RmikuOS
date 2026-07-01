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

static inline void uvprintf(const char *fmt, va_list ap) {
    struct uprintf_buf b; b.len = 0;
    while (*fmt) {
        char ch = *fmt++;
        if (ch != '%') { uprintf_putc(&b, ch); continue; }
        int is_long = 0;
        if (*fmt == 'l') { is_long = 1; fmt++; }
        char spec = *fmt;
        if (spec == 0) { uprintf_putc(&b, '%'); break; }
        fmt++;
        switch (spec) {
            case 'd': {
                if (is_long) { long v = va_arg(ap, long); uprintf_i64_dec(&b, (long long)v); }
                else { int v = va_arg(ap, int); uprintf_i64_dec(&b, (long long)v); }
                break;
            }
            case 'u': {
                if (is_long) { unsigned long v = va_arg(ap, unsigned long); uprintf_u64_dec(&b, (unsigned long long)v); }
                else { unsigned int v = va_arg(ap, unsigned int); uprintf_u64_dec(&b, (unsigned long long)v); }
                break;
            }
            case 'x': {
                if (is_long) { unsigned long v = va_arg(ap, unsigned long); uprintf_u64_hex(&b, (unsigned long long)v); }
                else { unsigned int v = va_arg(ap, unsigned int); uprintf_u64_hex(&b, (unsigned long long)v); }
                break;
            }
            case 'p': {
                void *v = va_arg(ap, void *);
                uprintf_putc(&b, '0'); uprintf_putc(&b, 'x');
                uprintf_u64_hex(&b, (unsigned long long)(usize)v);
                break;
            }
            case 'c': { int v = va_arg(ap, int); uprintf_putc(&b, (char)v); break; }
            case 's': { const char *v = va_arg(ap, const char *); uprintf_puts_raw(&b, v); break; }
            case '%': { uprintf_putc(&b, '%'); break; }
            default: {
                uprintf_putc(&b, '%'); if (is_long) uprintf_putc(&b, 'l'); uprintf_putc(&b, spec);
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
// 注意：mv::Tuple3 有 3 个 public 成员(first, second, third)，
// 编译器可以直接做结构化绑定，不需要 tuple_size/tuple_element/get
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
        operator bool() const { return ok_; }
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
    };
    inline bool getline(ifstream& f, string& out) { return f.getline(out); }

    class istringstream {
        char* ptr_;
        char* end_;
    public:
        istringstream() : ptr_(nullptr), end_(nullptr) {}
        istringstream(const char* s) { str(s); }
        istringstream(const string& s) { str(s.c_str()); }
        void str(const char* s) { ptr_ = (char*)s; end_ = ptr_ + (s ? mystr::strlen(s) : 0); }
        void str(const string& s) { str(s.c_str()); }

        istringstream& operator>>(string& out) {
            out = string();
            while (ptr_ < end_ && (*ptr_ == ' ' || *ptr_ == '\t' || *ptr_ == '\r' || *ptr_ == '\n')) ptr_++;
            if (ptr_ >= end_) return *this;
            while (ptr_ < end_ && *ptr_ != ' ' && *ptr_ != '\t' && *ptr_ != '\r' && *ptr_ != '\n') {
                out += *ptr_++;
            }
            return *this;
        }
        operator bool() const { return ptr_ < end_; }
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
            bool operator==(const iterator& o) const { return p == o.p; }  // ← 新增
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

// ========== std::cstdio (printf → uprintf) ==========
namespace std {
    inline int printf(const char* fmt, ...) {
        va_list ap; va_start(ap, fmt); uvprintf(fmt, ap); va_end(ap);
        return 0;
    }
    inline int fprintf(int fd, const char* fmt, ...) {
        (void)fd;
        va_list ap; va_start(ap, fmt); uvprintf(fmt, ap); va_end(ap);
        return 0;
    }
}

// C 风格 printf 也桥接
inline int printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); uvprintf(fmt, ap); va_end(ap);
    return 0;
}
inline int fprintf(int fd, const char* fmt, ...) {
    (void)fd;
    va_list ap; va_start(ap, fmt); uvprintf(fmt, ap); va_end(ap);
    return 0;
}