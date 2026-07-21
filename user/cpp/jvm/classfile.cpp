#include "classfile.h"
#include "my/stdcompat.h"

struct Reader {
    const uint8_t* p;
    uint8_t  u1() { return *p++; }
    uint16_t u2() { uint16_t v = (p[0]<<8)|p[1]; p+=2; return v; }
    uint32_t u4() { uint32_t v=(u2()<<16)|u2(); return v; }
};

std::string ClassFile::cp_class_name(int i) const {
    if (i == 0) return "java/lang/Object";
    return cp_utf8(cp[i].u.class_name_idx);
}
std::string ClassFile::cp_name_and_type(int i, bool name) const {
    const CPInfo& nt = cp[i];
    if (name) return cp_utf8(nt.u.nt.name_idx);
    return cp_utf8(nt.u.nt.desc_idx);
}
Method* ClassFile::find_method(const std::string& n, const std::string& d) {
    for (auto& m : methods) if (m.name == n && m.desc == d) return &m;
    return nullptr;
}
Field* ClassFile::find_field(const std::string& n, const std::string& d) {
    for (auto& f : fields) if (f.name == n && f.desc == d) return &f;
    return nullptr;
}

ClassFile parse_class(const uint8_t* buf) {
    Reader r{buf};
    if (r.u4() != 0xCAFEBABE) { jvm_panic("bad magic"); }
    r.u2(); r.u2(); // minor, major
    ClassFile cf;
    int n = r.u2();
    cf.cp.resize(n);
    for (int i = 1; i < n; i++) {
        uint8_t tag = r.u1(); cf.cp[i].tag = tag;
        switch (tag) {
        case 1: { // Utf8
            int len = r.u2();
            cf.cp[i].str.resize(len);
            for (int j = 0; j < len; j++) cf.cp[i].str[j] = r.p[j];
            r.p += len;
            break;
        }
        case 7: case 8: cf.cp[i].u.class_name_idx = r.u2(); break;
        case 3: cf.cp[i].u.int_val = (int32_t)r.u4(); break;
        case 4: { union {uint32_t u; float f;} x; x.u=r.u4(); cf.cp[i].u.float_val=x.f; break; }
        case 5: { union {uint64_t u; int64_t j;} x; x.u=((uint64_t)r.u4()<<32)|r.u4(); cf.cp[i].u.long_val=x.j; i++; break; }
        case 6: { union {uint64_t u; double d;} x; x.u=((uint64_t)r.u4()<<32)|r.u4(); cf.cp[i].u.double_val=x.d; i++; break; }
        case 9: case 10: case 11:
            cf.cp[i].u.ref.class_idx = r.u2();
            cf.cp[i].u.ref.name_type_idx = r.u2();
            break;
        case 12:
            cf.cp[i].u.nt.name_idx = r.u2();
            cf.cp[i].u.nt.desc_idx = r.u2();
            break;
        case 15: r.u1(); r.u2(); break;
        case 16: r.u2(); break;
        case 17: case 18: r.u2(); r.u2(); break;
        default: { char b[40]; snprintf(b,40,"bad cp tag %d\n",tag); jvm_panic(b); }
        }
    }
    cf.flags = r.u2();
    cf.this_class = r.u2();
    cf.super_class = r.u2();
    for (int i = r.u2(); i > 0; i--) cf.interfaces.push_back(r.u2());

    auto read_attrs = [&](Method* m) {
        for (int ac = r.u2(); ac > 0; ac--) {
            uint16_t ai = r.u2(); uint32_t len = r.u4();
            if (m && cf.cp_utf8(ai) == "Code") {
                m->max_stack = r.u2(); m->max_locals = r.u2();
                uint32_t cl = r.u4();
                m->code.resize(cl);
                for (uint32_t j = 0; j < cl; j++) m->code[j] = r.p[j];
                r.p += cl;
                for (int e = r.u2(); e > 0; e--) {
                    ExceptionEntry ex;
                    ex.start_pc = r.u2(); ex.end_pc = r.u2();
                    ex.handler_pc = r.u2(); ex.catch_type = r.u2();
                    m->exceptions.push_back(ex);
                }
                for (int a2 = r.u2(); a2 > 0; a2--) { r.u2(); r.p += r.u4(); }
            } else r.p += len;
        }
    };

    for (int cnt = r.u2(); cnt > 0; cnt--) {
        Field f; f.flags = r.u2();
        f.name = cf.cp_utf8(r.u2()); f.desc = cf.cp_utf8(r.u2());
        f.offset = (int)cf.fields.size();
        read_attrs(nullptr);
        cf.fields.push_back(f);
    }
    for (int cnt = r.u2(); cnt > 0; cnt--) {
        Method m; m.flags = r.u2();
        m.name = cf.cp_utf8(r.u2()); m.desc = cf.cp_utf8(r.u2());
        read_attrs(&m);
        cf.methods.push_back(std::move(m));
    }
    return cf;
}