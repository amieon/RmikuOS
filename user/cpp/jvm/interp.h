#include "heap.h"
#pragma once
#include "types.h"

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
    void maybe_gc();

    Value exec(Method* m, ClassFile* cf, std::vector<Value> args);
    Value invoke(Method* m, ClassFile* cf, std::vector<Value> args);
    void throw_ex(const std::string& name);
};
