// aot.cpp —— 装载期 AOT：字节码 -> 本机机器码（RISC-V64 / LoongArch64）
//
// 模板式编译：
//   - locals / 操作数栈是内存 8 字节槽（AotFrame），和解释器 Frame 语义一致
//   - 算术/分支/加载存储内联，其余调用 C++ helper
//   - GC 对 AOT 帧链保守扫描（槽值在 heap.objects 中即视为引用）
//   - 含不支持 opcode 的方法整体 fallback 回解释器

#include "aot.h"
#include "interp.h"
#include "heap.h"
#include "my/stdcompat.h"
#include "mem.h"
#include "aot_common.h"
#if defined(__riscv) && (__riscv_xlen == 64)
#include "aot_riscv64.h"
using AotBackend = Riscv64;
#define AOT_HAS_BACKEND 1
#elif defined(__loongarch) || defined(__loongarch__)
#include "aot_loongarch64.h"
using AotBackend = LoongArch64;
#define AOT_HAS_BACKEND 1
#endif


// ============================================================
// 第 0 部分：小工具（和 interp.cpp 里的静态函数同逻辑，独立一份）
// ============================================================

static int aot_slot_count(const std::string& desc) {
    int n = 0;
    for (size_t i = 1; i < desc.size() && desc[i] != ')'; ) {
        if (desc[i] == '[') { i++; while (desc[i] == '[') i++; }
        if (desc[i] == 'L') { while (desc[i] != ';' && desc[i] != ')') i++; i++; n++; }
        else if (desc[i] == 'J' || desc[i] == 'D') { n += 2; i++; }
        else { n++; i++; }
    }
    return n;
}

static ClassFile* aot_ensure_class(VM& vm, const std::string& name) {
    if (vm.classes.count(name)) return vm.classes[name];
    return load_class(vm, vm.classpath.empty() ? nullptr : vm.classpath.c_str(), name.c_str());
}

static Method* aot_resolve_method(ClassFile* start, const std::string& name, const std::string& desc) {
    ClassFile* cls = start;
    while (cls) {
        Method* m = cls->find_method(name, desc);
        if (m) return m;
        cls = cls->super;
    }
    return nullptr;
}

// u64 槽 -> Value：保守判断是否为堆对象引用
static Value slot_to_value(VM& vm, uint64_t slot) {
    if (slot != 0 && (slot & 7) == 0) {
        for (size_t i = 0; i < vm.heap.objects.size(); i++) {
            if ((uint64_t)vm.heap.objects[i] == slot)
                return Value::fromRef((Object*)slot);
        }
    }
    return Value::fromInt((int32_t)slot);
}

// Value -> u64 槽
static uint64_t value_to_slot(const Value& v) {
    if (v.type == T_REF) return (uint64_t)v.obj;
    return (uint64_t)(int64_t)v.i;
}

// ---- 栈操作（helper 视角）----
static void fr_push(AotFrame* fr, uint64_t v) {
    fr->stack[fr->sp / 8] = v;
    fr->sp += 8;
}
static uint64_t fr_pop(AotFrame* fr) {
    fr->sp -= 8;
    return fr->stack[fr->sp / 8];
}

// ============================================================
// 第 1 部分：helper 函数群（被编译码调用）
// 统一签名 void (*)(AotFrame*, uint64_t imm)
// ============================================================

static void aot_h_ldc(AotFrame* fr, uint64_t idx) {
    ClassFile* cf = fr->cf;
    const CPInfo& cp = cf->cp[idx];
    if (cp.tag == 3) {
        fr_push(fr, (uint64_t)(int64_t)cp.u.int_val);
    } else if (cp.tag == 8) {
        Object* s = fr->vm->heap.alloc_string(cf->cp_utf8(cp.u.string_idx).c_str());
        fr_push(fr, (uint64_t)s);
    } else {
        fprintf(stderr, "aot ldc unsupported tag %d\n", cp.tag);
        _exit(1);
    }
}

static void aot_h_new(AotFrame* fr, uint64_t idx) {
    VM& vm = *fr->vm;
    if (vm.heap.objects.size() >= vm.heap.gc_threshold) vm.heap.gc(vm);
    std::string clsname = fr->cf->cp_class_name((int)idx);
    ClassFile* target = aot_ensure_class(vm, clsname);
    if (!target) { fprintf(stderr, "new class not found: %s\n", clsname.c_str()); _exit(1); }
    fr_push(fr, (uint64_t)vm.heap.alloc_object(target));
}

static void aot_h_newarray(AotFrame* fr, uint64_t atype) {
    VM& vm = *fr->vm;
    if (vm.heap.objects.size() >= vm.heap.gc_threshold) vm.heap.gc(vm);
    int32_t len = (int32_t)fr_pop(fr);
    if (len < 0) { vm.throw_ex("java/lang/NegativeArraySizeException"); return; }
    ValueType et = T_INT;
    if (atype == 4 || atype == 8) et = T_BYTE;
    else if (atype == 5) et = T_CHAR;
    fr_push(fr, (uint64_t)vm.heap.alloc_array(len, et));
}

static void aot_h_anewarray(AotFrame* fr, uint64_t idx) {
    (void)idx;
    VM& vm = *fr->vm;
    if (vm.heap.objects.size() >= vm.heap.gc_threshold) vm.heap.gc(vm);
    int32_t len = (int32_t)fr_pop(fr);
    if (len < 0) { vm.throw_ex("java/lang/NegativeArraySizeException"); return; }
    fr_push(fr, (uint64_t)vm.heap.alloc_array(len, T_REF));
}

static void aot_h_getstatic(AotFrame* fr, uint64_t idx) {
    VM& vm = *fr->vm;
    ClassFile* cf = fr->cf;
    const CPInfo& fld = cf->cp[idx];
    std::string clsname = cf->cp_class_name(fld.u.ref.class_idx);
    std::string fname = cf->cp_name_and_type(fld.u.ref.name_type_idx, true);
    std::string fdesc = cf->cp_name_and_type(fld.u.ref.name_type_idx, false);
    ClassFile* target = aot_ensure_class(vm, clsname);
    if (!target) { fprintf(stderr, "class not found: %s\n", clsname.c_str()); _exit(1); }
    Field* field = target->find_field(fname, fdesc);
    if (!field) { fprintf(stderr, "field not found: %s.%s\n", clsname.c_str(), fname.c_str()); _exit(1); }
    fr_push(fr, value_to_slot(field->static_value));
}

static void aot_h_putstatic(AotFrame* fr, uint64_t idx) {
    VM& vm = *fr->vm;
    ClassFile* cf = fr->cf;
    const CPInfo& fld = cf->cp[idx];
    std::string clsname = cf->cp_class_name(fld.u.ref.class_idx);
    std::string fname = cf->cp_name_and_type(fld.u.ref.name_type_idx, true);
    std::string fdesc = cf->cp_name_and_type(fld.u.ref.name_type_idx, false);
    ClassFile* target = aot_ensure_class(vm, clsname);
    if (!target) { fprintf(stderr, "class not found: %s\n", clsname.c_str()); _exit(1); }
    Field* field = target->find_field(fname, fdesc);
    if (!field) { fprintf(stderr, "field not found: %s.%s\n", clsname.c_str(), fname.c_str()); _exit(1); }
    field->static_value = slot_to_value(vm, fr_pop(fr));
}

static void aot_h_getfield(AotFrame* fr, uint64_t idx) {
    VM& vm = *fr->vm;
    ClassFile* cf = fr->cf;
    Object* obj = (Object*)fr_pop(fr);
    if (!obj) { vm.throw_ex("java/lang/NullPointerException"); return; }
    const CPInfo& fld = cf->cp[idx];
    std::string fname = cf->cp_name_and_type(fld.u.ref.name_type_idx, true);
    std::string fdesc = cf->cp_name_and_type(fld.u.ref.name_type_idx, false);
    Field* field = obj->clazz->find_field(fname, fdesc);
    if (!field) { fprintf(stderr, "field not found: %s\n", fname.c_str()); _exit(1); }
    fr_push(fr, value_to_slot(obj->fields[field->offset]));
}

static void aot_h_putfield(AotFrame* fr, uint64_t idx) {
    VM& vm = *fr->vm;
    ClassFile* cf = fr->cf;
    uint64_t v = fr_pop(fr);
    Object* obj = (Object*)fr_pop(fr);
    if (!obj) { vm.throw_ex("java/lang/NullPointerException"); return; }
    const CPInfo& fld = cf->cp[idx];
    std::string fname = cf->cp_name_and_type(fld.u.ref.name_type_idx, true);
    std::string fdesc = cf->cp_name_and_type(fld.u.ref.name_type_idx, false);
    Field* field = obj->clazz->find_field(fname, fdesc);
    if (!field) { fprintf(stderr, "field not found: %s\n", fname.c_str()); _exit(1); }
    obj->fields[field->offset] = slot_to_value(vm, v);
}

// invoke 共用：从操作数栈收参数、调用、压返回值
static void aot_do_invoke(AotFrame* fr, ClassFile* target, Method* m,
                          const std::string& desc, bool has_this) {
    VM& vm = *fr->vm;
    int nargs = aot_slot_count(desc) + (has_this ? 1 : 0);
    fr->sp -= (uint32_t)nargs * 8;
    std::vector<Value> args(nargs);
    for (int i = 0; i < nargs; i++)
        args[i] = slot_to_value(vm, fr->stack[fr->sp / 8 + i]);
    Value ret = vm.invoke(m, target, args);
    if (vm.exception_obj) return;  // 编译码调用点会检查 exc_slot
    size_t rp = desc.find(')');
    if (rp != std::string::npos) {
        std::string rdesc = desc.substr(rp + 1);
        if (rdesc != "V" && rdesc != "") fr_push(fr, value_to_slot(ret));
    }
}

static void aot_h_invokestatic(AotFrame* fr, uint64_t idx) {
    VM& vm = *fr->vm;
    ClassFile* cf = fr->cf;
    const CPInfo& mref = cf->cp[idx];
    std::string clsname = cf->cp_class_name(mref.u.ref.class_idx);
    std::string name = cf->cp_name_and_type(mref.u.ref.name_type_idx, true);
    std::string desc = cf->cp_name_and_type(mref.u.ref.name_type_idx, false);
    ClassFile* target = aot_ensure_class(vm, clsname);
    if (!target) { fprintf(stderr, "class not found: %s\n", clsname.c_str()); _exit(1); }
    Method* m = aot_resolve_method(target, name, desc);
    if (!m) { fprintf(stderr, "static not found: %s.%s%s\n", clsname.c_str(), name.c_str(), desc.c_str()); _exit(1); }
    aot_do_invoke(fr, target, m, desc, false);
}

static void aot_h_invokespecial(AotFrame* fr, uint64_t idx) {
    VM& vm = *fr->vm;
    ClassFile* cf = fr->cf;
    const CPInfo& mref = cf->cp[idx];
    std::string clsname = cf->cp_class_name(mref.u.ref.class_idx);
    std::string name = cf->cp_name_and_type(mref.u.ref.name_type_idx, true);
    std::string desc = cf->cp_name_and_type(mref.u.ref.name_type_idx, false);
    ClassFile* target = aot_ensure_class(vm, clsname);
    if (!target) { fprintf(stderr, "class not found: %s\n", clsname.c_str()); _exit(1); }
    Method* m = target->find_method(name, desc);
    if (!m) m = aot_resolve_method(target, name, desc);
    if (!m) { fprintf(stderr, "special not found: %s.%s%s\n", clsname.c_str(), name.c_str(), desc.c_str()); _exit(1); }
    aot_do_invoke(fr, target, m, desc, true);
}

static void aot_h_invokevirtual(AotFrame* fr, uint64_t idx) {
    VM& vm = *fr->vm;
    ClassFile* cf = fr->cf;
    const CPInfo& mref = cf->cp[idx];
    std::string name = cf->cp_name_and_type(mref.u.ref.name_type_idx, true);
    std::string desc = cf->cp_name_and_type(mref.u.ref.name_type_idx, false);
    // this 在参数栈底
    int nargs = aot_slot_count(desc);
    uint64_t this_slot = fr->stack[fr->sp / 8 - nargs - 1];
    Object* obj = (Object*)this_slot;
    if (!obj) { vm.throw_ex("java/lang/NullPointerException"); return; }
    Method* m = aot_resolve_method(obj->clazz, name, desc);
    if (!m) { fprintf(stderr, "method not found: %s%s\n", name.c_str(), desc.c_str()); _exit(1); }
    aot_do_invoke(fr, obj->clazz, m, desc, true);
}

// ---- 数组访问 ----
static Object* aot_arr_check(AotFrame* fr, int32_t idx) {
    Object* arr = (Object*)fr_pop(fr);
    if (!arr || idx < 0 || idx >= arr->array_length) {
        if (!arr) fr->vm->throw_ex("java/lang/NullPointerException");
        else fr->vm->throw_ex("java/lang/ArrayIndexOutOfBoundsException");
        return nullptr;
    }
    return arr;
}

static void aot_h_iaload(AotFrame* fr, uint64_t imm) {
    (void)imm;
    int32_t idx = (int32_t)fr_pop(fr);
    Object* arr = aot_arr_check(fr, idx);
    if (!arr) return;
    fr_push(fr, (uint64_t)(int64_t)arr->fields[idx].asInt());
}
static void aot_h_aaload(AotFrame* fr, uint64_t imm) {
    (void)imm;
    int32_t idx = (int32_t)fr_pop(fr);
    Object* arr = aot_arr_check(fr, idx);
    if (!arr) return;
    fr_push(fr, value_to_slot(arr->fields[idx]));
}
static void aot_h_baload(AotFrame* fr, uint64_t imm) {
    (void)imm;
    int32_t idx = (int32_t)fr_pop(fr);
    Object* arr = aot_arr_check(fr, idx);
    if (!arr) return;
    fr_push(fr, (uint64_t)(int64_t)(int8_t)arr->fields[idx].asInt());
}
static void aot_h_iastore(AotFrame* fr, uint64_t imm) {
    (void)imm;
    uint64_t v = fr_pop(fr);
    int32_t idx = (int32_t)fr_pop(fr);
    Object* arr = aot_arr_check(fr, idx);
    if (!arr) return;
    arr->fields[idx] = Value::fromInt((int32_t)v);
}
static void aot_h_aastore(AotFrame* fr, uint64_t imm) {
    (void)imm;
    uint64_t v = fr_pop(fr);
    int32_t idx = (int32_t)fr_pop(fr);
    Object* arr = aot_arr_check(fr, idx);
    if (!arr) return;
    arr->fields[idx] = slot_to_value(*fr->vm, v);
}
static void aot_h_bastore(AotFrame* fr, uint64_t imm) {
    (void)imm;
    uint64_t v = fr_pop(fr);
    int32_t idx = (int32_t)fr_pop(fr);
    Object* arr = aot_arr_check(fr, idx);
    if (!arr) return;
    arr->fields[idx] = Value::fromInt((int32_t)(int8_t)v);
}
static void aot_h_arraylength(AotFrame* fr, uint64_t imm) {
    (void)imm;
    Object* arr = (Object*)fr_pop(fr);
    if (!arr) { fr->vm->throw_ex("java/lang/NullPointerException"); return; }
    fr_push(fr, (uint64_t)(int64_t)arr->array_length);
}
static void aot_h_athrow(AotFrame* fr, uint64_t imm) {
    (void)imm;
    Object* ex = (Object*)fr_pop(fr);
    if (!ex) { fr->vm->throw_ex("java/lang/NullPointerException"); return; }
    fr->vm->exception_obj = ex;
}
static void aot_h_throw_arith(AotFrame* fr, uint64_t imm) {
    (void)imm;
    fr->vm->throw_ex("java/lang/ArithmeticException");
}

static const void* g_helper_table[H_COUNT] = {
    (const void*)aot_h_ldc, (const void*)aot_h_new,
    (const void*)aot_h_newarray, (const void*)aot_h_anewarray,
    (const void*)aot_h_getstatic, (const void*)aot_h_putstatic,
    (const void*)aot_h_getfield, (const void*)aot_h_putfield,
    (const void*)aot_h_invokevirtual, (const void*)aot_h_invokespecial,
    (const void*)aot_h_invokestatic,
    (const void*)aot_h_iaload, (const void*)aot_h_aaload, (const void*)aot_h_baload,
    (const void*)aot_h_iastore, (const void*)aot_h_aastore, (const void*)aot_h_bastore,
    (const void*)aot_h_arraylength, (const void*)aot_h_athrow,
    (const void*)aot_h_throw_arith,
};




// ============================================================
// 第 2 部分：发射器 + 架构无关编译驱动
// 后端 = 一个提供全部 static emit 函数的 struct（Riscv64 / LoongArch64）
// ============================================================

// ============================================================
// 第 5 部分：编译入口 + 执行
// ============================================================
#ifdef AOT_HAS_BACKEND

bool aot_compile_method(Method* m, ClassFile* cf) {
    if (m->is_native() || m->code.empty()) return false;
    if (m->max_locals > 250 || m->max_stack > 250) return false;

    uint32_t cap = 16384;
    uint8_t* page = (uint8_t*)mmap(cap, PROT_READ | PROT_WRITE | PROT_EXEC);
    if ((isize)page < 0) return false;

    Emit e;
    e.buf = page + TABLE_BYTES;
    e.cap = cap - TABLE_BYTES;
    if (!aot_compile_generic<AotBackend>(m->code.data(), (uint32_t)m->code.size(), e)) {
        munmap(page, cap);
        return false;
    }

    // 页头填 helper 地址表
    uint64_t* table = (uint64_t*)page;
    for (int i = 0; i < H_COUNT; i++) table[i] = (uint64_t)g_helper_table[i];

    AotBackend::flush_icache(page, TABLE_BYTES + e.len);
    m->aot_code_base = page + TABLE_BYTES;
    m->aot_entry = (void*)(page + TABLE_BYTES);
    return true;
}

void aot_compile_class(ClassFile* cf) {
    for (auto& m : cf->methods) {
        if (m.aot_entry || m.aot_failed) continue;
        if (!aot_compile_method(&m, cf)) m.aot_failed = true;
    }
}

Value aot_exec(VM& vm, Method* m, ClassFile* cf, std::vector<Value>& args) {
    uint64_t locals_buf[256];
    uint64_t stack_buf[256];
    for (uint32_t i = 0; i < m->max_locals; i++) locals_buf[i] = 0;
    for (size_t i = 0; i < args.size() && i < m->max_locals; i++)
        locals_buf[i] = value_to_slot(args[i]);

    AotFrame fr;
    fr.locals = locals_buf;
    fr.stack = stack_buf;
    fr.sp = 0;
    fr.n_locals = m->max_locals;
    fr.n_stack = m->max_stack;
    fr.cf = cf;
    fr.method = m;
    fr.vm = &vm;
    fr.exc_slot = &vm.exception_obj;
    fr.code_base = (uint8_t*)m->aot_code_base;
    fr.parent = vm.aot_top;
    vm.aot_top = &fr;

    uint64_t r = ((AotEntry)m->aot_entry)(&fr);

    vm.aot_top = fr.parent;

    // 返回类型从描述符判断
    size_t rp = m->desc.find(')');
    if (rp != std::string::npos && rp + 1 < m->desc.size()) {
        char t = m->desc[rp + 1];
        if (t == 'L' || t == '[') return Value::fromRef((Object*)r);
    }
    return Value::fromInt((int32_t)r);
}

#else  // 没有后端的架构（如 x86 宿主机测试）：全部走解释器

bool aot_compile_method(Method*, ClassFile*) { return false; }
void aot_compile_class(ClassFile*) {}
Value aot_exec(VM&, Method*, ClassFile*, std::vector<Value>&) { return Value(); }

#endif