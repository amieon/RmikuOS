#pragma once

#include "compat.h"
#include "vector.h"
#include "cmath.h"
#include "random.h"
#include "string.h"
#include "io.h"
#include "map.h"
#include "set.h"
#include "../fmt.h"

using usize = unsigned long;


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

// ========== std::vector / pair / tuple / map / set 别名 ==========
namespace std {
    template<typename T> using vector = mv::Vector<T>;
    template<typename T> using set = my::set<T>;
    template<typename T1, typename T2> using map = my::map<T1, T2>;
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
            buf.clear();
            if (s) { size_t n = mystr::strlen(s); for (size_t i = 0; i < n; i++) buf.push_back(s[i]); }
            buf.push_back('\0');
            return *this;
        }
        string& operator=(const string& o) { buf = o.buf; return *this; }
        
        const char* c_str() const { return buf.data(); }
        size_t size() const { return buf.empty() ? 0 : buf.size() - 1; }
        bool empty() const { return size() == 0; }
        
        char& operator[](size_t i) { return buf[i]; }
        char operator[](size_t i) const { return buf[i]; }
        char& front() { return buf[0]; }
        char& back() { return buf[size() - 1]; }   // 调用者保证非空
        
        bool operator<(const string& o) const { return mystr::strcmp(c_str(), o.c_str()) < 0; }
        
        void clear() { buf.clear(); buf.push_back('\0'); }
        void reserve(size_t n) { buf.reserve(n + 1); }
        void resize(size_t n) { buf.resize(n + 1); buf[n] = '\0'; }
        
        void push_back(char c) {
            if (!buf.empty()) buf.pop_back();   // 弹掉旧的 '\0'
            buf.push_back(c);
            buf.push_back('\0');
        }
        
        char pop_back() {
            if (buf.size() <= 1) { clear(); return '\0'; }  // 空字符串，保持空
            buf.pop_back();           // 弹掉 '\0'
            char ret = buf.back();    // 取最后一个字符
            buf.pop_back();           // 弹掉它
            buf.push_back('\0');      // 恢复 '\0'
            return ret;
        }
        
        string& operator+=(const string& o) {
            buf.pop_back();
            for (size_t i = 0; i < o.size(); i++) buf.push_back(o[i]);
            buf.push_back('\0');
            return *this;
        }
        string& operator+=(const char* s) {
            buf.pop_back();
            size_t n = mystr::strlen(s);
            for (size_t i = 0; i < n; i++) buf.push_back(s[i]);
            buf.push_back('\0');
            return *this;
        }
        string& operator+=(char c) {
            buf.pop_back();
            buf.push_back(c);
            buf.push_back('\0');
            return *this;
        }
        
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
        size_t size() {return ok_ ? sz_ : 0;}

        bool getline(string& out) {
            out = string();
            if (!ok_ || !cur_ || (size_t)(cur_ - buf_) >= sz_) return false;
            while ((size_t)(cur_ - buf_) < sz_ && *cur_ != '\n') { out += *cur_++; }
            if ((size_t)(cur_ - buf_) < sz_ && *cur_ == '\n') cur_++;   
            if (out.size() > 0 && out[out.size() - 1] == '\r') {
                out[out.size() - 1] = '\0';
            }
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
        char* ptr_; char* end_; bool ok_; bool last_success_;
    public:
        istringstream() : ptr_(nullptr), end_(nullptr), ok_(false), last_success_(false) {}
        istringstream(const char* s) { str(s); }
        istringstream(const string& s) { str(s.c_str()); }
        void str(const char* s) { 
            ptr_ = (char*)s; 
            end_ = ptr_ + (s ? mystr::strlen(s) : 0); 
            ok_ = (s != nullptr); 
            last_success_ = false;
        }
        void str(const string& s) { str(s.c_str()); }

        istringstream& operator>>(string& out) {
            out = string();
            last_success_ = false;
            if (!ok_ || ptr_ >= end_) { ok_ = false; return *this; }
            while (ptr_ < end_ && (*ptr_ == ' ' || *ptr_ == '\t' || *ptr_ == '\r' || *ptr_ == '\n')) ptr_++;
            if (ptr_ >= end_) { ok_ = false; return *this; }
            while (ptr_ < end_ && *ptr_ != ' ' && *ptr_ != '\t' && *ptr_ != '\r' && *ptr_ != '\n') {
                out += *ptr_++;
                last_success_ = true;
            }
            return *this;
        }
        operator bool() const { return last_success_; }
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

