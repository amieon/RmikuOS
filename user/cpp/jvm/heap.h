#pragma once
#include "types.h"

struct Heap {
    std::vector<Object*> objects;
    size_t gc_threshold = 256;  // 对象数超过就触发 GC
    size_t total_alloc = 0;

    Object* alloc_object(ClassFile* clazz);
    Object* alloc_array(int32_t length, ValueType elem_type);
    Object* alloc_string(const char* str);
    void gc(VM& vm);            // STW Mark-Sweep
private:
    void mark(VM& vm);
    void sweep();
    void mark_object(Object* obj);
    void mark_value(const Value& v);
};
