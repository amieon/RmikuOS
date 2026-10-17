#pragma once
#include <stdio.h>
#include <string>
#include <vector>

#include "Tensor.h"

namespace st {

// ---- 小工具 ---------------------------------------------------------------

inline unsigned long long rd_u64le(const unsigned char* p) {
    unsigned long long v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | (unsigned long long)p[i];
    return v;
}

inline void wr_u64le(unsigned char* p, unsigned long long v) {
    for (int i = 0; i < 8; ++i) p[i] = (unsigned char)((v >> (8 * i)) & 0xff);
}

// 不依赖 std::to_string(RmikuOS 的 std 子集里未必有)
inline void append_uint(std::string& s, unsigned long long v) {
    char b[24];
    int n = 0;
    if (v == 0) b[n++] = '0';
    while (v) { b[n++] = (char)('0' + (int)(v % 10)); v /= 10; }
    for (int i = n - 1; i >= 0; --i) s.push_back(b[i]);
}

// dtype 字符串 → 每元素字节数; 未知返回 0
inline size_t dtype_size(const std::string& d) {
    if (d == "F64" || d == "I64") return 8;
    if (d == "F32" || d == "I32") return 4;
    if (d == "F16" || d == "BF16" || d == "I16") return 2;
    if (d == "I8" || d == "U8" || d == "BOOL") return 1;
    return 0;
}

// ---- 位重解释: 逐字节做, 不依赖 memcpy 也不假设宿主字节序 ----
inline float f32_from_bits(unsigned int bits) {
    float v;
    unsigned char* p = (unsigned char*)&v;
    for (int b = 0; b < 4; ++b) p[b] = (unsigned char)((bits >> (8 * b)) & 0xff);
    return v;
}
inline unsigned int bits_from_f32(float v) {
    const unsigned char* p = (const unsigned char*)&v;
    unsigned int bits = 0;
    for (int b = 3; b >= 0; --b) bits = (bits << 8) | (unsigned int)p[b];
    return bits;
}
inline double f64_from_bits(unsigned long long bits) {
    double v;
    unsigned char* p = (unsigned char*)&v;
    for (int b = 0; b < 8; ++b) p[b] = (unsigned char)((bits >> (8 * b)) & 0xff);
    return v;
}

struct TensorInfo {
    std::string name;
    std::string dtype;
    std::vector<long long> shape;
    unsigned long long begin = 0, end = 0;   // 相对数据区起点
};

// ---- 极简 JSON 扫描器(只为 safetensors header 服务, 不建树) --------------

class Scanner {
    const char* p_;
    const char* e_;

public:
    Scanner(const char* p, const char* e) : p_(p), e_(e) {}

    void ws() {
        while (p_ < e_ && (*p_ == ' ' || *p_ == '\t' || *p_ == '\n' || *p_ == '\r')) ++p_;
    }
    bool lit(char c) {
        ws();
        if (p_ < e_ && *p_ == c) { ++p_; return true; }
        return false;
    }

    // 读字符串, 处理 \" \\ \/ \b \f \n \r \t \uXXXX
    bool str(std::string& out, std::string& err) {
        out.clear();
        ws();
        if (p_ >= e_ || *p_ != '"') { err = "期望字符串"; return false; }
        ++p_;
        while (p_ < e_ && *p_ != '"') {
            if (*p_ != '\\') { out.push_back(*p_++); continue; }
            ++p_;
            if (p_ >= e_) { err = "转义序列被截断"; return false; }
            char c = *p_++;
            switch (c) {
                case 'n': out.push_back('\n'); break;
                case 't': out.push_back('\t'); break;
                case 'r': out.push_back('\r'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case '/': out.push_back('/');  break;
                case '"': out.push_back('"');  break;
                case '\\': out.push_back('\\'); break;
                case 'u': {
                    if (e_ - p_ < 4) { err = "\\u 转义被截断"; return false; }
                    unsigned cp = 0;
                    for (int i = 0; i < 4; ++i) {
                        char h = p_[i];
                        unsigned d;
                        if (h >= '0' && h <= '9')      d = (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') d = (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') d = (unsigned)(h - 'A' + 10);
                        else { err = "\\u 含非法十六进制"; return false; }
                        cp = cp * 16 + d;
                    }
                    p_ += 4;
                    if (cp < 0x80) {
                        out.push_back((char)cp);
                    } else if (cp < 0x800) {
                        out.push_back((char)(0xC0 | (cp >> 6)));
                        out.push_back((char)(0x80 | (cp & 0x3F)));
                    } else {
                        out.push_back((char)(0xE0 | (cp >> 12)));
                        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
                        out.push_back((char)(0x80 | (cp & 0x3F)));
                    }
                    break;
                }
                default: err = "未知转义字符"; return false;
            }
        }
        if (p_ >= e_) { err = "字符串未闭合"; return false; }
        ++p_;                                   // 吃掉右引号
        return true;
    }

    // 当前扫描位置(用于截取 __metadata__ 的原始文本)
    const char* pos() const { return p_; }

    bool integer(long long& out, std::string& err) {
        ws();
        bool neg = false;
        if (p_ < e_ && (*p_ == '-' || *p_ == '+')) { neg = (*p_ == '-'); ++p_; }
        if (p_ >= e_ || *p_ < '0' || *p_ > '9') { err = "期望整数"; return false; }
        long long v = 0;
        while (p_ < e_ && *p_ >= '0' && *p_ <= '9') v = v * 10 + (*p_++ - '0');
        out = neg ? -v : v;
        return true;
    }

    // 跳过任意 JSON 值(用括号计数); 用于略过 __metadata__
    bool skip_value(std::string& err) {
        ws();
        if (p_ >= e_) { err = "跳过时遇到结尾"; return false; }
        int depth = 0;
        for (;;) {
            if (p_ >= e_) { err = "跳过时遇到结尾"; return false; }
            char c = *p_;
            if (c == '"') { std::string dummy; if (!str(dummy, err)) return false; if (depth == 0) return true; continue; }
            if (c == '{' || c == '[') { ++depth; ++p_; continue; }
            if (c == '}' || c == ']') { --depth; ++p_; if (depth <= 0) return true; continue; }
            if (c == ',' && depth == 0) return true;
            ++p_;
        }
    }
};

inline bool parse_header(const char* json, size_t len, std::string* meta_out,
                         std::vector<TensorInfo>& out, std::string& err) {
    Scanner s(json, json + len);
    if (!s.lit('{')) { err = "header 必须以 '{' 开头"; return false; }
    s.ws();
    if (s.lit('}')) return true;                       // 空 header
    for (;;) {
        std::string name;
        if (!s.str(name, err)) return false;
        if (!s.lit(':')) { err = "期望 ':'"; return false; }

        // 顶层 __metadata__ 是 string→string 映射, 不是张量; 记录原始文本备用
        if (name == "__metadata__") {
            const char* mb = s.pos();
            if (!s.skip_value(err)) return false;
            if (meta_out) meta_out->assign(mb, (size_t)(s.pos() - mb));
        } else {
            if (!s.lit('{')) { err = std::string("期望张量描述对象: ") + name; return false; }
            TensorInfo ti;
            ti.name = name;
            s.ws();
            while (!s.lit('}')) {
                std::string key;
                if (!s.str(key, err)) return false;
                if (!s.lit(':')) { err = "期望 ':'"; return false; }
                if (key == "dtype") {
                    if (!s.str(ti.dtype, err)) return false;
                } else if (key == "shape" || key == "data_offsets") {
                    if (!s.lit('[')) { err = "期望 '['"; return false; }
                    std::vector<long long> vals;
                    if (!s.lit(']')) {
                        for (;;) {
                            long long v;
                            if (!s.integer(v, err)) return false;
                            vals.push_back(v);
                            s.ws();
                            if (s.lit(',')) continue;
                            if (s.lit(']')) break;
                            err = "数组未闭合";
                            return false;
                        }
                    }
                    if (key == "shape") {
                        ti.shape = vals;
                    } else {
                        if (vals.size() != 2) { err = "data_offsets 需两个元素"; return false; }
                        ti.begin = (unsigned long long)vals[0];
                        ti.end   = (unsigned long long)vals[1];
                    }
                } else {
                    err = std::string("未知字段: ") + key;
                    return false;
                }
                s.ws();
                if (s.lit(',')) continue;
                if (s.lit('}')) break;
                err = "张量描述对象未闭合";
                return false;
            }
            out.push_back(ti);
        }

        s.ws();
        if (s.lit(',')) continue;
        if (s.lit('}')) break;
        err = "header 未闭合";
        return false;
    }
    return true;
}

// ---- 读 -------------------------------------------------------------------

struct File {
    std::vector<TensorInfo> tensors;
    std::vector<unsigned char> data;      // 数据区(整个读进内存, 小模型足够)
    std::string meta_json;                // __metadata__ 的原始 JSON 文本(可为空)

    const TensorInfo* find(const char* name) const {
        for (size_t i = 0; i < tensors.size(); ++i)
            if (tensors[i].name == name) return &tensors[i];
        return 0;
    }
};

// 从 __metadata__ 里取一个字符串值(如 features / classes / hidden)。
// 用法: std::string v; if (st::meta_str(f, "features", v, err)) ...
inline bool meta_str(const File& f, const char* key, std::string& out, std::string& err) {
    out.clear();
    if (f.meta_json.empty()) { err = "文件没有 __metadata__"; return false; }
    Scanner s(f.meta_json.data(), f.meta_json.data() + f.meta_json.size());
    if (!s.lit('{')) { err = "__metadata__ 不是对象"; return false; }
    s.ws();
    if (s.lit('}')) { err = "__metadata__ 是空的"; return false; }
    for (;;) {
        std::string k;
        if (!s.str(k, err)) return false;
        if (!s.lit(':')) { err = "期望 ':'"; return false; }
        if (k == key) return s.str(out, err);
        std::string dummy;
        if (!s.str(dummy, err)) return false;      // 值只能是 string
        s.ws();
        if (s.lit(',')) continue;
        if (s.lit('}')) break;
        err = "__metadata__ 未闭合";
        return false;
    }
    err = std::string("__metadata__ 里没有键: ") + key;
    return false;
}

inline bool load(const char* path, File& out, std::string& err) {
    FILE* f = fopen(path, "rb");
    if (!f) { err = "无法打开: "; err += path; return false; }

    unsigned char len_b[8];
    if (fread(len_b, 1, 8, f) != 8) { fclose(f); err = "读 header 长度失败"; return false; }
    const unsigned long long n = rd_u64le(len_b);
    if (n == 0 || n > (1ULL << 28)) { fclose(f); err = "header 长度异常"; return false; }

    std::string json((size_t)n, '\0');
    if (fread(&json[0], 1, (size_t)n, f) != n) { fclose(f); err = "读 header 失败"; return false; }

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); err = "fseek(END) 失败"; return false; }
    long fsz = ftell(f);
    if (fsz < 0 || (unsigned long long)fsz < 8 + n) {
        fclose(f);
        err = "文件长度小于 8+header";
        return false;
    }
    const size_t dsz = (size_t)((unsigned long long)fsz - 8 - n);
    if (fseek(f, (long)(8 + n), SEEK_SET) != 0) { fclose(f); err = "fseek(SET) 失败"; return false; }
    out.data.resize(dsz);
    if (dsz && fread(out.data.data(), 1, dsz, f) != dsz) {
        fclose(f);
        err = "读数据区失败";
        return false;
    }
    fclose(f);

    return parse_header(json.data(), (size_t)n, &out.meta_json, out.tensors, err);
}

// 取一个二维 float 张量(F32 原生 / F64 降精度)
inline bool get_f32(const File& f, const char* name,
                    DenseMatrix<float>& out, std::string& err) {
    const TensorInfo* ti = f.find(name);
    if (!ti) { err = std::string("找不到张量: ") + name; return false; }

    const size_t esz = dtype_size(ti->dtype);
    if (esz == 0) { err = std::string("不支持的 dtype: ") + ti->dtype; return false; }
    if (ti->shape.size() != 2) { err = std::string("只支持二维张量: ") + name; return false; }

    const int r = (int)ti->shape[0];
    const int c = (int)ti->shape[1];
    const size_t cnt = (size_t)r * (size_t)c;
    const size_t need = cnt * esz;
    if (ti->end - ti->begin != need) { err = std::string("data_offsets 与 shape 不符: ") + name; return false; }
    if (ti->end > f.data.size()) { err = std::string("数据越界: ") + name; return false; }

    const unsigned char* src = f.data.data() + ti->begin;
    out = DenseMatrix<float>(r, c);

    if (ti->dtype == "F32") {
        for (size_t i = 0; i < cnt; ++i) {           // 逐字节组装小端
            unsigned int bits = 0;
            for (int b = 3; b >= 0; --b) bits = (bits << 8) | (unsigned int)src[i * 4 + b];
            out.data[i] = f32_from_bits(bits);
        }
        return true;
    }
    if (ti->dtype == "F64") {
        for (size_t i = 0; i < cnt; ++i) {
            unsigned long long bits = 0;
            for (int b = 7; b >= 0; --b) bits = (bits << 8) | (unsigned long long)src[i * 8 + b];
            out.data[i] = (float)f64_from_bits(bits);
        }
        return true;
    }
    err = std::string("本实现只转 F32/F64: ") + ti->dtype;
    return false;
}

// ---- 写 -------------------------------------------------------------------

struct Entry {
    const char* name;
    const DenseMatrix<float>* m;
};

// header 尾部按规范用 0x20 填充到 8 的倍数(规范是 MAY, 但与官方实现保持一致)
// meta_json: 可选的 __metadata__ 内容, 形如 {"features":"1433","classes":"7"};
//            传 nullptr 则不写。规范要求它的值必须全是字符串, 所以数字也要加引号。
inline bool save(const char* path, const Entry* entries, int count,
                 const char* meta_json, std::string& err) {
    if (count <= 0) { err = "没有要保存的张量"; return false; }

    std::string h = "{";
    if (meta_json && meta_json[0] == '{') {
        h += "\"__metadata__\":";
        h += meta_json;
        h += ",";
    }
    unsigned long long off = 0;
    for (int i = 0; i < count; ++i) {
        const DenseMatrix<float>& m = *entries[i].m;
        const unsigned long long bytes = (unsigned long long)m.rows * (unsigned long long)m.cols * 4ULL;
        h.push_back('"');
        h += entries[i].name;
        h += "\":{\"dtype\":\"F32\",\"shape\":[";
        append_uint(h, (unsigned long long)m.rows);
        h += ",";
        append_uint(h, (unsigned long long)m.cols);
        h += "],\"data_offsets\":[";
        append_uint(h, off);
        h += ",";
        append_uint(h, off + bytes);
        h += "]}";
        if (i + 1 < count) h += ",";
        off += bytes;
    }
    h += "}";
    while (h.size() % 8 != 0) h.push_back(' ');

    FILE* f = fopen(path, "wb");
    if (!f) { err = "无法创建文件: "; err += path; return false; }

    unsigned char len_b[8];
    wr_u64le(len_b, (unsigned long long)h.size());
    if (fwrite(len_b, 1, 8, f) != 8) { fclose(f); err = "写 header 长度失败"; return false; }
    if (fwrite(h.data(), 1, h.size(), f) != h.size()) { fclose(f); err = "写 header 失败"; return false; }

    for (int i = 0; i < count; ++i) {
        const DenseMatrix<float>& m = *entries[i].m;
        const size_t cnt = (size_t)m.rows * (size_t)m.cols;
        std::vector<unsigned char> raw(cnt * 4);
        for (size_t k = 0; k < cnt; ++k) {
            unsigned int bits = bits_from_f32(m.data[k]);
            for (int b = 0; b < 4; ++b) raw[k * 4 + b] = (unsigned char)((bits >> (8 * b)) & 0xff);
        }
        if (fwrite(raw.data(), 1, raw.size(), f) != raw.size()) {
            fclose(f);
            err = "写数据区失败";
            return false;
        }
    }

    if (fflush(f) != 0) { fclose(f); err = "fflush 失败"; return false; }
    fclose(f);
    // 注: 这里只保证进了内核缓冲区。RmikuOS 上由周期 writeback 落盘;
    //     需要立即持久化可再对文件描述符调用 fsync。
    return true;
}

}  // namespace st
