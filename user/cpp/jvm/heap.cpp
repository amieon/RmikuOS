#include "heap.h"
#include "interp.h"
#include "my/stdcompat.h"
#include "aot.h"

Object* Heap::alloc_object(ClassFile* clazz) {
    Object* o = new Object();
    o->clazz = clazz;
    o->fields.resize(clazz->fields.size());
    for (auto& f : clazz->fields) {
        if (f.is_static()) continue;
        o->fields[f.offset] = f.static_value;
    }
    objects.push_back(o);
    total_alloc++;
    return o;
}
Object* Heap::alloc_array(int32_t length, ValueType elem_type) {
    Object* o = new Object();
    o->array_length = length;
    o->fields.resize(length);
    for (int i=0;i<length;i++) o->fields[i] = Value::fromInt(0);
    (void)elem_type;
    objects.push_back(o);
    total_alloc++;
    return o;
}
Object* Heap::alloc_string(const char* str) {
    Object* o = new Object();
    o->hack_str = str;
    objects.push_back(o);
    total_alloc++;
    return o;
}

// ---------- GC ----------
void Heap::mark_value(const Value& v) {
    if (v.type == T_REF && v.obj) mark_object(v.obj);
}

void Heap::mark_object(Object* obj) {
    if (!obj || obj->marked) return;
    obj->marked = true;
    for (auto& f : obj->fields) mark_value(f);
}

void Heap::mark(VM& vm) {
    // 1. 标记所有栈帧里的局部变量和操作数栈
    for (auto& fr : vm.frames) {
        for (auto& v : fr.locals) mark_value(v);
        for (auto& v : fr.stack) mark_value(v);
    }
    // 2. 标记当前正在执行的帧（不在 vm.frames 里，在 exec 的局部变量 f 里）
    //    这个由调用者保证：gc 只在 exec 返回后、frames 完整时触发
    // 3. 标记所有类的静态字段
    for (auto kv : vm.classes) {
        for (auto& f : kv.second->fields) {
            if (f.is_static()) mark_value(f.static_value);
        }
    }
    // 4. 标记异常对象
    if (vm.exception_obj) mark_object(vm.exception_obj);
    // 5. 保守扫描 AOT 帧链：槽值若在 objects 表中即视为引用
    for (AotFrame* fr = vm.aot_top; fr; fr = fr->parent) {
        uint32_t nslots = fr->n_locals + fr->sp / 8;
        for (uint32_t i = 0; i < fr->n_locals; i++) {
            uint64_t v = fr->locals[i];
            if (!v || (v & 7)) continue;
            for (auto& o : objects)
                if ((uint64_t)o == v) { mark_object(o); break; }
        }
        for (uint32_t i = 0; i < fr->sp / 8; i++) {
            uint64_t v = fr->stack[i];
            if (!v || (v & 7)) continue;
            for (auto& o : objects)
                if ((uint64_t)o == v) { mark_object(o); break; }
        }
        (void)nslots;
    }
}

void Heap::sweep() {
    size_t live = 0, dead = 0;
    for (size_t i = 0; i < objects.size(); ) {
        if (objects[i]->marked) {
            objects[i]->marked = false;  // 清除标记，下次 GC 用
            i++;
            live++;
        } else {
            delete objects[i];
            objects[i] = objects.back();
            objects.pop_back();
            dead++;
        }
    }
    gc_threshold = std::max(size_t(64), live * 2);  // 下次 GC 阈值 = 存活对象 * 2
}

void Heap::gc(VM& vm) {
    mark(vm);
    sweep();
}