#pragma once
#include "my/stdcompat.h"

inline void jvm_panic(const char* msg) {
    printf("JVM panic: %s\n", msg);
    io::exit(1);
}

inline void _exit(int code) {
    io::exit(code);
}

template<typename T>
inline T jvm_max(T a, T b) { return a > b ? a : b; }

enum ValueType {
    T_INT = 0, T_LONG, T_FLOAT, T_DOUBLE, T_REF,
    T_BYTE, T_BOOL, T_CHAR, T_SHORT, T_VOID, T_DUMMY
};

struct Object;
struct ClassFile;
struct Method;
struct Field;
struct VM;
struct Frame;

struct Value {
    union {
        int32_t i;
        int64_t j;
        float f;
        double d;
        Object* obj;
    };
    ValueType type;
    Value() : i(0), type(T_INT) {}
    static Value fromInt(int32_t v) { Value r; r.i = v; r.type = T_INT; return r; }
    static Value fromRef(Object* o) { Value r; r.obj = o; r.type = T_REF; return r; }
    static Value fromByte(int8_t v) { Value r; r.i = v; r.type = T_BYTE; return r; }
    static Value fromBool(bool v) { Value r; r.i = v ? 1 : 0; r.type = T_BOOL; return r; }
    static Value fromChar(int16_t v) { Value r; r.i = v; r.type = T_CHAR; return r; }
    int32_t asInt() const { return i; }
};

struct Object {
    bool marked = false;
    ClassFile* clazz = nullptr;
    int32_t array_length = -1;
    std::vector<Value> fields;
    std::string hack_str; // for ldc string literals (simplified)
};

struct Field {
    uint16_t flags = 0;
    std::string name;
    std::string desc;
    int offset = 0;
    Value static_value;
    bool is_static() const { return flags & 0x0008; }
};

struct ExceptionEntry {
    uint16_t start_pc = 0, end_pc = 0, handler_pc = 0, catch_type = 0;
};

struct Method {
    uint16_t flags = 0;
    std::string name;
    std::string desc;
    std::vector<uint8_t> code;
    uint16_t max_stack = 0, max_locals = 0;
    std::vector<ExceptionEntry> exceptions;
    // ---- AOT：装载期编译产物 ----
    void* aot_entry = nullptr;      // 编译后入口 uint64_t (*)(AotFrame*)
    void* aot_code_base = nullptr;  // 代码页基址（helper 表在代码之前的区域）
    bool aot_failed = false;        // 编译失败，走解释器
    bool is_static() const { return flags & 0x0008; }
    bool is_native() const { return flags & 0x0100; }
    bool is_private() const { return flags & 0x0002; }
    bool is_init() const { return name == "<init>" || name == "<clinit>"; }
};

struct CPInfo {
    uint8_t tag = 0;
    std::string str;
    union {
        struct { uint16_t class_idx, name_type_idx; } ref;
        struct { uint16_t name_idx, desc_idx; } nt;
        uint16_t string_idx;
        uint16_t class_name_idx;
        int32_t int_val;
        float float_val;
        int64_t long_val;
        double double_val;
    } u;
};

struct ClassFile {
    std::vector<CPInfo> cp;
    uint16_t flags = 0, this_class = 0, super_class = 0;
    std::vector<uint16_t> interfaces;
    std::vector<Field> fields;
    std::vector<Method> methods;
    ClassFile* super = nullptr;

    std::string cp_utf8(int i) const { return cp[i].str; }
    std::string cp_class_name(int i) const;
    std::string cp_name_and_type(int i, bool name) const;
    Method* find_method(const std::string& n, const std::string& d);
    Field* find_field(const std::string& n, const std::string& d);
};

using NativeFunc = Value (*)(VM&, std::vector<Value>&);