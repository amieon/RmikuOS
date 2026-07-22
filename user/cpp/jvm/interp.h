#include "heap.h"
#pragma once
#include "types.h"

struct AotFrame;

struct Frame {
    Method* method = nullptr;
    ClassFile* clazz = nullptr;
    std::vector<Value> locals;
    std::vector<Value> stack;
    uint32_t pc = 0;
};

struct VM {
    Heap heap;
    ClassFile* main_class = nullptr;
    std::vector<Frame> frames;
    std::map<std::string, ClassFile*> classes;
    std::map<std::string, NativeFunc> natives;
    Object* exception_obj = nullptr;
    std::string classpath;   // 懒加载用，空字符串 = 当前目录
    AotFrame* aot_top = nullptr;   // AOT 帧链头（GC 保守扫描用）
    void maybe_gc();

    Value exec(Method* m, ClassFile* cf, std::vector<Value> args);
    Value invoke(Method* m, ClassFile* cf, std::vector<Value> args);
    void throw_ex(const std::string& name);
};

// 定义在 main.cpp；classpath 为 nullptr 表示当前目录
ClassFile* load_class(VM& vm, const char* classpath, const char* classname);