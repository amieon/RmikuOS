#include "interp.h"
#include "native.h"
#include "heap.h"
#include "my/stdcompat.h"
#include "aot.h"

// helpers
static int32_t popi(std::vector<Value>& s) { Value v=s.back(); s.pop_back(); return v.asInt(); }
static Object* popo(std::vector<Value>& s) { Value v=s.back(); s.pop_back(); return v.obj; }
static void pusho(std::vector<Value>& s, Object* o) { s.push_back(Value::fromRef(o)); }
static void pushi(std::vector<Value>& s, int32_t v) { s.push_back(Value::fromInt(v)); }

static int slot_count(const std::string& desc) {
    int n = 0;
    for (size_t i=1; i<desc.size() && desc[i]!=')'; ) {
        if (desc[i]=='[') { i++; while (desc[i]=='[') i++; }
        if (desc[i]=='L') { while (desc[i]!=';' && desc[i]!=')') i++; i++; n++; }
        else if (desc[i]=='J' || desc[i]=='D') { n+=2; i++; }
        else { n++; i++; }
    }
    return n;
}

// 类没找到时按 classpath 懒加载（load_class 定义在 main.cpp）
static ClassFile* ensure_class(VM& vm, const std::string& name) {
    if (vm.classes.count(name)) return vm.classes[name];
    return load_class(vm, vm.classpath.empty() ? nullptr : vm.classpath.c_str(), name.c_str());
}

static Method* resolve_method(ClassFile* start, const std::string& name, const std::string& desc) {
    ClassFile* cls = start;
    while (cls) {
        Method* m = cls->find_method(name, desc);
        if (m) return m;
        cls = cls->super;
    }
    return nullptr;
}

Value VM::invoke(Method* m, ClassFile* cf, std::vector<Value> args) {
    if (m->is_native()) {
        std::string clsname = cf->cp_class_name(cf->this_class);
        return call_native(*this, clsname, m->name, m->desc, args);
    }
    if (m->aot_entry) return aot_exec(*this, m, cf, args);
    return exec(m, cf, args);
}

Value VM::exec(Method* m, ClassFile* cf, std::vector<Value> args) {
    Frame f;
    f.method = m;
    f.clazz = cf;
    f.locals.resize(m->max_locals);
    f.stack.reserve(m->max_stack);
    for (size_t i=0;i<args.size() && i<f.locals.size();i++) f.locals[i]=args[i];
    const uint8_t* code = m->code.data();
    auto s2 = [&]{ int16_t v=(code[f.pc]<<8)|code[f.pc+1]; f.pc+=2; return v; };

    for (;;) {
        if (exception_obj) {
            // search exception handler
            bool handled = false;
            for (auto& e : m->exceptions) {
                if (f.pc >= e.start_pc && f.pc < e.end_pc) {
                    if (e.catch_type == 0) { // finally
                        f.stack.clear();
                        f.stack.push_back(Value::fromRef(exception_obj));
                        f.pc = e.handler_pc;
                        exception_obj = nullptr;
                        handled = true; break;
                    }
                    // simplified: exact match only
                    std::string catch_name = cf->cp_class_name(e.catch_type);
                    if (catch_name == "java/lang/Exception" || catch_name == "java/lang/Throwable" || catch_name == "java/lang/RuntimeException") {
                        f.stack.clear();
                        f.stack.push_back(Value::fromRef(exception_obj));
                        f.pc = e.handler_pc;
                        exception_obj = nullptr;
                        handled = true; break;
                    }
                }
            }
            if (!handled) {
                frames.push_back(std::move(f));
                return Value(); // unwind; caller will see exception_obj
            }
        }

        uint32_t opaddr = f.pc;
        uint8_t op = code[f.pc++];
        switch (op) {
        // ----- constants -----
        case 0x01: f.stack.push_back(Value::fromRef(nullptr)); break; // aconst_null
        case 0x02: pushi(f.stack, -1); break; // iconst_m1
        case 0x03: case 0x04: case 0x05: case 0x06: case 0x07: case 0x08:
            pushi(f.stack, op-3); break; // iconst_0..5
        case 0x10: pushi(f.stack, (int8_t)code[f.pc++]); break; // bipush
        case 0x11: pushi(f.stack, s2()); break; // sipush
        case 0x12: { // ldc
            uint8_t idx = code[f.pc++];
            const CPInfo& cp = cf->cp[idx];
            if (cp.tag==3) pushi(f.stack, cp.u.int_val);
            else if (cp.tag==8) pusho(f.stack, heap.alloc_string(cf->cp_utf8(cp.u.string_idx).c_str()));
            else { fprintf(stderr,"ldc unsupported tag %d\n",cp.tag); _exit(1); }
            break; }
        case 0x13: { // ldc_w
            uint16_t idx = s2();
            const CPInfo& cp = cf->cp[idx];
            if (cp.tag==3) pushi(f.stack, cp.u.int_val);
            else if (cp.tag==8) pusho(f.stack, heap.alloc_string(cf->cp_utf8(cp.u.string_idx).c_str()));
            else { fprintf(stderr,"ldc_w unsupported tag %d\n",cp.tag); _exit(1); }
            break; }
        // ----- loads -----
        case 0x15: pushi(f.stack, f.locals[code[f.pc++]].asInt()); break; // iload
        case 0x19: f.stack.push_back(f.locals[code[f.pc++]]); break; // aload
        case 0x1a: case 0x1b: case 0x1c: case 0x1d: pushi(f.stack, f.locals[op-0x1a].asInt()); break; // iload_0..3
        case 0x2a: case 0x2b: case 0x2c: case 0x2d: f.stack.push_back(f.locals[op-0x2a]); break; // aload_0..3
        case 0x2e: { // iaload
            int32_t idx = popi(f.stack); Object* arr = popo(f.stack);
            if (!arr || idx<0 || idx>=arr->array_length) { throw_ex("java/lang/ArrayIndexOutOfBoundsException"); break; }
            pushi(f.stack, arr->fields[idx].asInt()); break; }
        case 0x32: { // aaload
            int32_t idx = popi(f.stack); Object* arr = popo(f.stack);
            if (!arr || idx<0 || idx>=arr->array_length) { throw_ex("java/lang/ArrayIndexOutOfBoundsException"); break; }
            f.stack.push_back(arr->fields[idx]); break; }
        case 0x33: { // baload
            int32_t idx = popi(f.stack); Object* arr = popo(f.stack);
            if (!arr || idx<0 || idx>=arr->array_length) { throw_ex("java/lang/ArrayIndexOutOfBoundsException"); break; }
            pushi(f.stack, arr->fields[idx].asInt()); break; }
        // ----- stores -----
        case 0x36: f.locals[code[f.pc++]] = Value::fromInt(popi(f.stack)); break; // istore
        case 0x3a: f.locals[code[f.pc++]] = f.stack.back(); f.stack.pop_back(); break; // astore
        case 0x3b: case 0x3c: case 0x3d: case 0x3e: f.locals[op-0x3b] = Value::fromInt(popi(f.stack)); break; // istore_0..3
        case 0x4b: case 0x4c: case 0x4d: case 0x4e: f.locals[op-0x4b] = f.stack.back(); f.stack.pop_back(); break; // astore_0..3
        case 0x4f: { // iastore
            int32_t v = popi(f.stack); int32_t idx = popi(f.stack); Object* arr = popo(f.stack);
            if (!arr || idx<0 || idx>=arr->array_length) { throw_ex("java/lang/ArrayIndexOutOfBoundsException"); break; }
            arr->fields[idx] = Value::fromInt(v); break; }
        case 0x53: { // aastore
            Value v = f.stack.back(); f.stack.pop_back(); int32_t idx = popi(f.stack); Object* arr = popo(f.stack);
            if (!arr || idx<0 || idx>=arr->array_length) { throw_ex("java/lang/ArrayIndexOutOfBoundsException"); break; }
            arr->fields[idx] = v; break; }
        case 0x54: { // bastore
            int32_t v = popi(f.stack); int32_t idx = popi(f.stack); Object* arr = popo(f.stack);
            if (!arr || idx<0 || idx>=arr->array_length) { throw_ex("java/lang/ArrayIndexOutOfBoundsException"); break; }
            arr->fields[idx] = Value::fromInt(v); break; }
        // ----- stack -----
        case 0x57: f.stack.pop_back(); break; // pop
        case 0x59: f.stack.push_back(f.stack.back()); break; // dup
        case 0x5f: { Value v1=f.stack.back(); f.stack.pop_back(); Value v2=f.stack.back(); f.stack.pop_back();
                     f.stack.push_back(v1); f.stack.push_back(v2); break; } // swap
        // ----- math -----
        case 0x60: { int32_t b=popi(f.stack),a=popi(f.stack); pushi(f.stack,a+b); break; } // iadd
        case 0x64: { int32_t b=popi(f.stack),a=popi(f.stack); pushi(f.stack,a-b); break; } // isub
        case 0x68: { int32_t b=popi(f.stack),a=popi(f.stack); pushi(f.stack,a*b); break; } // imul
        case 0x6c: { int32_t b=popi(f.stack); if(b==0){throw_ex("java/lang/ArithmeticException");break;} pushi(f.stack,popi(f.stack)/b); break; } // idiv
        case 0x70: { int32_t b=popi(f.stack); if(b==0){throw_ex("java/lang/ArithmeticException");break;} pushi(f.stack,popi(f.stack)%b); break; } // irem
        case 0x74: pushi(f.stack, -popi(f.stack)); break; // ineg
        case 0x78: { int32_t s=popi(f.stack)&0x1f, a=popi(f.stack); pushi(f.stack,a<<s); break; } // ishl
        case 0x7a: { int32_t s=popi(f.stack)&0x1f, a=popi(f.stack); pushi(f.stack,a>>s); break; } // ishr
        case 0x7c: { int32_t s=popi(f.stack)&0x1f, a=popi(f.stack); pushi(f.stack,(uint32_t)a>>s); break; } // iushr
        case 0x7e: { int32_t b=popi(f.stack),a=popi(f.stack); pushi(f.stack,a&b); break; } // iand
        case 0x80: { int32_t b=popi(f.stack),a=popi(f.stack); pushi(f.stack,a|b); break; } // ior
        case 0x82: { int32_t b=popi(f.stack),a=popi(f.stack); pushi(f.stack,a^b); break; } // ixor
        case 0x84: { uint8_t idx=code[f.pc++]; f.locals[idx].i += (int8_t)code[f.pc++]; break; } // iinc
        case 0xc4: { // wide 前缀（javac 对 |增量|>127 的 iinc 会生成 wide iinc）
            uint8_t sub = code[f.pc++];
            if (sub == 0x84) {          // wide iinc
                uint16_t idx = (uint16_t)((code[f.pc] << 8) | code[f.pc+1]); f.pc += 2;
                int16_t cst = s2();
                f.locals[idx].i += cst;
            } else if (sub == 0x15 || sub == 0x17 || sub == 0x19) {  // wide iload/fload/aload
                uint16_t idx = (uint16_t)((code[f.pc] << 8) | code[f.pc+1]); f.pc += 2;
                f.stack.push_back(f.locals[idx]);
            } else if (sub == 0x36 || sub == 0x38 || sub == 0x3a) {  // wide istore/fstore/astore
                uint16_t idx = (uint16_t)((code[f.pc] << 8) | code[f.pc+1]); f.pc += 2;
                f.locals[idx] = f.stack.back(); f.stack.pop_back();
            } else { char b[64]; int l=snprintf(b,64,"unimplemented wide opcode 0x%02x\n",sub); write(2,b,l); _exit(1); }
            break; }
        // ----- conversions -----
        case 0x91: { int32_t v=popi(f.stack); pushi(f.stack,(int8_t)v); break; } // i2b
        case 0x92: { int32_t v=popi(f.stack); pushi(f.stack,(int16_t)v); break; } // i2c
        case 0x93: { int32_t v=popi(f.stack); pushi(f.stack,(int16_t)v); break; } // i2s
        // ----- comparisons -----
        case 0x9f: case 0xa0: case 0xa1: case 0xa2: case 0xa3: case 0xa4: {
            int16_t off=s2(); int32_t b=popi(f.stack),a=popi(f.stack); bool t;
            switch(op){case 0x9f:t=a==b;break;case 0xa0:t=a!=b;break;case 0xa1:t=a<b;break;
                       case 0xa2:t=a>=b;break;case 0xa3:t=a>b;break;default:t=a<=b;}
            if (t) { f.pc = opaddr + off; } break; }
        case 0x99: case 0x9a: case 0x9b: case 0x9c: case 0x9d: case 0x9e: {
            int16_t off=s2(); int32_t a=popi(f.stack); bool t;
            switch(op){case 0x99:t=a==0;break;case 0x9a:t=a!=0;break;case 0x9b:t=a<0;break;
                       case 0x9c:t=a>=0;break;case 0x9d:t=a>0;break;default:t=a<=0;}
            if (t) { f.pc = opaddr + off; } break; }
        case 0xc6: { int16_t off=s2(); Object* o=popo(f.stack); if(!o) f.pc=opaddr+off; break; } // ifnull
        case 0xc7: { int16_t off=s2(); Object* o=popo(f.stack); if(o) f.pc=opaddr+off; break; } // ifnonnull
        case 0xa5: case 0xa6: { // if_acmpeq / if_acmpne
            int16_t off=s2(); Object* b=popo(f.stack); Object* a=popo(f.stack);
            if ((op==0xa5 && a==b) || (op==0xa6 && a!=b)) f.pc = opaddr + off;
            break; }
        // ----- control -----
        case 0xa7: f.pc = opaddr + s2(); break; // goto
        case 0xac: { Value v = f.stack.back(); f.stack.pop_back(); return v; } // ireturn
        case 0xb0: { Value v = f.stack.back(); f.stack.pop_back(); return v; } // areturn
        case 0xb1: return Value(); // return
        // ----- references -----
        case 0xb2: { // getstatic
            uint16_t idx = s2();
            const CPInfo& fld = cf->cp[idx];
            std::string clsname = cf->cp_class_name(fld.u.ref.class_idx);
            std::string fname = cf->cp_name_and_type(fld.u.ref.name_type_idx, true);
            std::string fdesc = cf->cp_name_and_type(fld.u.ref.name_type_idx, false);
            ClassFile* target = ensure_class(*this, clsname);
            if (!target) { fprintf(stderr,"class not found: %s\n",clsname.c_str()); _exit(1); }
            Field* field = target->find_field(fname, fdesc);
            if (!field) { fprintf(stderr,"field not found: %s.%s\n",clsname.c_str(),fname.c_str()); _exit(1); }
            f.stack.push_back(field->static_value); break; }
        case 0xb3: { // putstatic
            uint16_t idx = s2();
            const CPInfo& fld = cf->cp[idx];
            std::string clsname = cf->cp_class_name(fld.u.ref.class_idx);
            std::string fname = cf->cp_name_and_type(fld.u.ref.name_type_idx, true);
            std::string fdesc = cf->cp_name_and_type(fld.u.ref.name_type_idx, false);
            ClassFile* target = ensure_class(*this, clsname);
            if (!target) { fprintf(stderr,"class not found: %s\n",clsname.c_str()); _exit(1); }
            Field* field = target->find_field(fname, fdesc);
            if (!field) { fprintf(stderr,"field not found: %s.%s\n",clsname.c_str(),fname.c_str()); _exit(1); }
            field->static_value = f.stack.back(); f.stack.pop_back(); break; }
        case 0xb4: { // getfield
            uint16_t idx = s2();
            Object* obj = popo(f.stack);
            if (!obj) { throw_ex("java/lang/NullPointerException"); break; }
            const CPInfo& fld = cf->cp[idx];
            std::string fname = cf->cp_name_and_type(fld.u.ref.name_type_idx, true);
            std::string fdesc = cf->cp_name_and_type(fld.u.ref.name_type_idx, false);
            Field* field = obj->clazz->find_field(fname, fdesc);
            if (!field) { fprintf(stderr,"field not found: %s\n",fname.c_str()); _exit(1); }
            f.stack.push_back(obj->fields[field->offset]); break; }
        case 0xb5: { // putfield
            uint16_t idx = s2();
            Value v = f.stack.back(); f.stack.pop_back();
            Object* obj = popo(f.stack);
            if (!obj) { throw_ex("java/lang/NullPointerException"); break; }
            const CPInfo& fld = cf->cp[idx];
            std::string fname = cf->cp_name_and_type(fld.u.ref.name_type_idx, true);
            std::string fdesc = cf->cp_name_and_type(fld.u.ref.name_type_idx, false);
            Field* field = obj->clazz->find_field(fname, fdesc);
            if (!field) { fprintf(stderr,"field not found: %s\n",fname.c_str()); _exit(1); }
            obj->fields[field->offset] = v; break; }
        case 0xb6: { // invokevirtual
            uint16_t idx = s2();
            const CPInfo& mref = cf->cp[idx];
            std::string name = cf->cp_name_and_type(mref.u.ref.name_type_idx, true);
            std::string desc = cf->cp_name_and_type(mref.u.ref.name_type_idx, false);
            int nargs = slot_count(desc) + 1;  // +1: args[0] 是 this
            std::vector<Value> args(nargs);
            for (int i=nargs-1;i>=0;i--) { args[i]=f.stack.back(); f.stack.pop_back(); }
            Object* obj = args[0].obj;
            if (!obj) { throw_ex("java/lang/NullPointerException"); break; }
            Method* m = resolve_method(obj->clazz, name, desc);
            if (!m) { fprintf(stderr,"method not found: %s%s\n",name.c_str(),desc.c_str()); _exit(1); }
            Value ret = invoke(m, obj->clazz, args);
            if (exception_obj) {
                frames.push_back(std::move(f));
                return Value();
            }
            if (desc.find(')') != std::string::npos) {
                std::string rdesc = desc.substr(desc.find(')')+1);
                if (rdesc != "V" && rdesc != "") f.stack.push_back(ret);
            }
            break; }
        case 0xb7: { // invokespecial
            uint16_t idx = s2();
            const CPInfo& mref = cf->cp[idx];
            std::string clsname = cf->cp_class_name(mref.u.ref.class_idx);
            std::string name = cf->cp_name_and_type(mref.u.ref.name_type_idx, true);
            std::string desc = cf->cp_name_and_type(mref.u.ref.name_type_idx, false);
            int nargs = slot_count(desc) + 1;  // +1: args[0] 是 this
            std::vector<Value> args(nargs);
            for (int i=nargs-1;i>=0;i--) { args[i]=f.stack.back(); f.stack.pop_back(); }
            ClassFile* target = ensure_class(*this, clsname);
            if (!target) { fprintf(stderr,"class not found: %s\n",clsname.c_str()); _exit(1); }
            Method* m = target->find_method(name, desc);
            if (!m) m = resolve_method(target, name, desc);
            if (!m) { fprintf(stderr,"special not found: %s.%s%s\n",clsname.c_str(),name.c_str(),desc.c_str()); _exit(1); }
            Value ret = invoke(m, target, args);
            if (exception_obj) {
                frames.push_back(std::move(f));
                return Value();
            }
            if (desc.find(')') != std::string::npos) {
                std::string rdesc = desc.substr(desc.find(')')+1);
                if (rdesc != "V" && rdesc != "") f.stack.push_back(ret);
            }
            break; }
        case 0xb8: { // invokestatic
            uint16_t idx = s2();
            const CPInfo& mref = cf->cp[idx];
            std::string clsname = cf->cp_class_name(mref.u.ref.class_idx);
            std::string name = cf->cp_name_and_type(mref.u.ref.name_type_idx, true);
            std::string desc = cf->cp_name_and_type(mref.u.ref.name_type_idx, false);
            ClassFile* target = ensure_class(*this, clsname);
            if (!target) { fprintf(stderr,"class not found: %s\n",clsname.c_str()); _exit(1); }
            Method* m = target->find_method(name, desc);
            if (!m) m = resolve_method(target, name, desc);
            if (!m) { fprintf(stderr,"static not found: %s.%s%s\n",clsname.c_str(),name.c_str(),desc.c_str()); _exit(1); }
            int nargs = slot_count(desc);
            std::vector<Value> args(nargs);
            for (int i=nargs-1;i>=0;i--) { args[i]=f.stack.back(); f.stack.pop_back(); }
            Value ret = invoke(m, target, args);
            if (exception_obj) {
                frames.push_back(std::move(f));
                return Value();
            }
            if (desc.find(')') != std::string::npos) {
                std::string rdesc = desc.substr(desc.find(')')+1);
                if (rdesc != "V" && rdesc != "") f.stack.push_back(ret);
            }
            break; }
        case 0xbb: { // new
            if (heap.objects.size() >= heap.gc_threshold) {
                frames.push_back(std::move(f));
                heap.gc(*this);
                f = std::move(frames.back());
                frames.pop_back();
            }
            uint16_t idx = s2();
            // cp[idx] 是 Class 项，cp_class_name 内部会自己再取 name_idx，不能传两次
            std::string clsname = cf->cp_class_name(idx);
            ClassFile* target = ensure_class(*this, clsname);
            if (!target) { fprintf(stderr,"new class not found: %s\n",clsname.c_str()); _exit(1); }
            pusho(f.stack, heap.alloc_object(target)); break; }
        case 0xbc: { // newarray
            if (heap.objects.size() >= heap.gc_threshold) {
                frames.push_back(std::move(f));
                heap.gc(*this);
                f = std::move(frames.back());
                frames.pop_back();
            }
            uint8_t atype = code[f.pc++];
            int32_t len = popi(f.stack);
            if (len < 0) { throw_ex("java/lang/NegativeArraySizeException"); break; }
            ValueType et = T_INT;
            if (atype==4||atype==8) et=T_BYTE; else if (atype==5) et=T_CHAR; else if (atype==6) et=T_FLOAT;
            else if (atype==7) et=T_DOUBLE; else if (atype==9) et=T_SHORT; else if (atype==10) et=T_INT;
            else if (atype==11) et=T_LONG;
            pusho(f.stack, heap.alloc_array(len, et)); break; }
        case 0xbd: { // anewarray
            if (heap.objects.size() >= heap.gc_threshold) {
                frames.push_back(std::move(f));
                heap.gc(*this);
                f = std::move(frames.back());
                frames.pop_back();
            }
            s2(); int32_t len = popi(f.stack);
            if (len < 0) { throw_ex("java/lang/NegativeArraySizeException"); break; }
            pusho(f.stack, heap.alloc_array(len, T_REF)); break; }
        case 0xbe: { // arraylength
            Object* arr = popo(f.stack);
            if (!arr) { throw_ex("java/lang/NullPointerException"); break; }
            pushi(f.stack, arr->array_length); break; }
        case 0xbf: { // athrow
            Object* ex = popo(f.stack);
            if (!ex) { throw_ex("java/lang/NullPointerException"); break; }
            exception_obj = ex;
            break; }
        case 0xc0: case 0xc1: { // checkcast, instanceof
            s2(); f.stack.pop_back(); pushi(f.stack, 1); break; } // simplified: always succeed
        // ----- extended -----
        case 0xc2: case 0xc3: break; // monitorenter/exit (ignored in single-threaded)
        default:
            { char b[64]; int l=snprintf(b,64,"unimplemented opcode 0x%02x at pc=%u\n",op,opaddr); write(2,b,l); _exit(1); }
        }
    }
}

void VM::throw_ex(const std::string& name) {
    Object* ex = heap.alloc_string(name.c_str());
    ex->clazz = nullptr; // mark as exception
    exception_obj = ex;
}

void VM::maybe_gc() {
    if (heap.objects.size() >= heap.gc_threshold) {
        heap.gc(*this);
    }
}