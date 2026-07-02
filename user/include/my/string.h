#pragma once
#include "compat.h"
#include "../io.h"  
#include "flag.h"  



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

// 读取文件到 malloc 的 buffer（假设文件 < max_size，Cora 约 1MB，设 4MB 足够）
inline char* read_file(const char* path, size_t& out_size, size_t max_size = 4194304) {
    isize fd = open(path, O_RDONLY);
    if (fd < 0) { out_size = 0; return nullptr; }
    char* buf = (char*)malloc(max_size);
    if (!buf) { close((int)fd); out_size = 0; return nullptr; }
    size_t total = 0;
    while (total < max_size) {
        isize n = read((int)fd, buf + total, max_size - total);
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