#pragma once
#include "compat.h"
#include "../io.h"  
#include "flag.h"  

namespace my {
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

        // 比较
        bool operator>(const string& o) const { return mystr::strcmp(c_str(), o.c_str()) > 0; }
        bool operator>(const char* s) const { return mystr::strcmp(c_str(), s) > 0; }
        bool operator<=(const string& o) const { return !(*this > o); }
        bool operator>=(const string& o) const { return !(*this < o); }

        // 查找
        size_t find(const char* s, size_t pos = 0) const {
            if (!s || !*s) return 0;
            size_t n = mystr::strlen(s);
            size_t sz = size();
            if (pos >= sz) return npos;
            for (size_t i = pos; i + n <= sz; i++) {
                if (mystr::strncmp(c_str() + i, s, n) == 0) return i;
            }
            return npos;
        }
        size_t find(char c, size_t pos = 0) const {
            size_t sz = size();
            for (size_t i = pos; i < sz; i++) if (buf[i] == c) return i;
            return npos;
        }

        // 截取
        string substr(size_t pos = 0, size_t len = npos) const {
            size_t sz = size();
            if (pos > sz) pos = sz;
            size_t rlen = sz - pos;
            if (len < rlen) rlen = len;
            string res;
            res.reserve(rlen);
            for (size_t i = 0; i < rlen; i++) res.push_back(buf[pos + i]);
            return res;
        }
        
        void clear() { buf.clear(); buf.push_back('\0'); }
        void reserve(size_t n) { buf.reserve(n + 1); }
        void resize(size_t n) { buf.resize(n + 1); buf[n] = '\0'; }
        
        void push_back(char c) {
            if (!buf.empty()) buf.pop_back();   // 弹掉旧的 '\0'
            buf.push_back(c);
            buf.push_back('\0');
        }
        
        char pop_back() {
            if (buf.size() <= 1) { clear(); return '\0'; }  
            buf.pop_back();       
            char ret = buf.back();  
            buf.pop_back();         
            buf.push_back('\0');    
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

namespace mystr {

inline int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

inline void strcpy(char* dst, const char* src) {
    while ((*dst++ = *src++));
}

inline size_t strlen(const char* s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

inline int str_to_int(const char* s) {
    int sign = 1;
    if (*s == '-') { sign = -1; s++; }
    int val = 0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    return sign * val;
}

inline double str_to_double(const char* s) {
    double sign = 1.0;
    if (*s == '-') { sign = -1.0; s++; }
    double val = 0.0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    if (*s == '.') {
        s++;
        double frac = 0.1;
        while (*s >= '0' && *s <= '9') {
            val += (*s - '0') * frac;
            frac *= 0.1;
            s++;
        }
    }
    return sign * val;
}


inline char* read_file(const char* path, size_t& out_size, size_t init_size = 4194304) {
    isize fd = open(path, O_RDONLY);
    if (fd < 0) { out_size = 0; return nullptr; }
    
    size_t cap = init_size;
    char* buf = (char*)malloc(cap);
    if (!buf) { close((int)fd); out_size = 0; return nullptr; }
    
    size_t total = 0;
    while (1) {
        if (total >= cap) {
            // 缓冲区满了，扩容一倍
            size_t new_cap = cap * 2;
            char* new_buf = (char*)malloc(new_cap);
            if (!new_buf) break;  // 扩容失败，返回已读部分
            for (size_t i = 0; i < total; i++) new_buf[i] = buf[i];
            free(buf);
            buf = new_buf;
            cap = new_cap;
        }
        isize n = read((int)fd, buf + total, cap - total);
        if (n <= 0) break;
        total += (size_t)n;
    }
    
    close((int)fd);
    out_size = total;
    return buf;
}

// 按空格/制表符/换行分割一行，返回 token 数量
inline int split_line(char* line, char** tokens, int max_tokens) {
    int n = 0;
    while (*line && n < max_tokens) {
        while (*line == ' ' || *line == '\t' || *line == '\r') line++;
        if (!*line || *line == '\n') break;
        tokens[n++] = line;
        while (*line && *line != ' ' && *line != '\t' && *line != '\r' && *line != '\n') line++;
        if (*line) { *line = '\0'; line++; }
    }
    return n;
}

// 简易字符串->值映射（线性搜索），适用于小数据量（Cora 2708 条）
template <typename V>
struct SimpleMap {
    struct Entry {
        char key[48];
        V val;
    };
    mv::Vector<Entry> entries;

    SimpleMap() = default;

    V* find(const char* key) {
        for (size_t i = 0; i < entries.size(); i++)
            if (strcmp(entries[i].key, key) == 0)
                return &entries[i].val;
        return nullptr;
    }

    V& operator[](const char* key) {
        if (V* p = find(key)) return *p;
        Entry e;
        strcpy(e.key, key);
        entries.push_back(e);
        return entries.back().val;
    }

    size_t size() const { return entries.size(); }
};

} // namespace mystr