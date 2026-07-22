// aot_common.h —— AOT 基础设施（无 STL 依赖，宿主/裸机/交叉测试通用）
#pragma once
#include <stdint.h>
#include <stddef.h>

struct Object;
struct ClassFile;
struct Method;
struct VM;

// AOT 编译后方法的栈帧：locals/操作数栈都是 8 字节槽
struct AotFrame {
    uint64_t* locals;
    uint64_t* stack;
    uint32_t  sp;          // 操作数栈顶（字节偏移）
    uint32_t  n_locals;
    uint32_t  n_stack;
    ClassFile* cf;
    Method*   method;
    VM*       vm;
    Object**  exc_slot;
    uint8_t*  code_base;   // helper 表在负偏移处
    AotFrame* parent;
};

using AotEntry = uint64_t (*)(AotFrame*);

// helper 表编号（aot.cpp 填地址，编译码按负偏移取）
enum {
    H_LDC = 0, H_NEW, H_NEWARRAY, H_ANEWARRAY,
    H_GETSTATIC, H_PUTSTATIC, H_GETFIELD, H_PUTFIELD,
    H_INVOKEVIRTUAL, H_INVOKESPECIAL, H_INVOKESTATIC,
    H_IALOAD, H_AALOAD, H_BALOAD, H_IASTORE, H_AASTORE, H_BASTORE,
    H_ARRAYLENGTH, H_ATHROW, H_THROW_ARITH,
    H_COUNT
};
#define TABLE_BYTES 512

struct Emit {
    uint8_t* buf;
    uint32_t   cap;
    uint32_t   len = 0;
    bool       fail = false;

    void u8(uint8_t v) {
        if (len + 1 > cap) { fail = true; return; }
        buf[len++] = v;
    }
    void u32(uint32_t v) {
        u8(v & 0xff); u8((v >> 8) & 0xff); u8((v >> 16) & 0xff); u8((v >> 24) & 0xff);
    }
    void patch32(uint32_t at, uint32_t v) {
        buf[at] = v & 0xff; buf[at+1] = (v>>8) & 0xff;
        buf[at+2] = (v>>16) & 0xff; buf[at+3] = (v>>24) & 0xff;
    }
    uint32_t read32(uint32_t at) const {
        return (uint32_t)buf[at] | ((uint32_t)buf[at+1] << 8) |
               ((uint32_t)buf[at+2] << 16) | ((uint32_t)buf[at+3] << 24);
    }
};

enum FixKind { FK_BC = 0, FK_EPI_RET, FK_EPI_THROW, FK_DIVSTUB };
struct Fixup {
    uint32_t at;
    uint32_t bc_target;
    FixKind  kind;
    uint32_t stub_id;
};
#define MAX_FIX 256
#define MAX_DIV 32

static bool aot_add_fix(Fixup* fix, int& nfix, uint32_t at, uint32_t target,
                        FixKind k, uint32_t sid = 0) {
    if (nfix >= MAX_FIX) return false;
    fix[nfix].at = at; fix[nfix].bc_target = target;
    fix[nfix].kind = k; fix[nfix].stub_id = sid;
    nfix++;
    return true;
}

static int aot_off(int which) {
    switch (which) {
    case 0: return (int)(size_t)&((AotFrame*)0)->locals;
    case 1: return (int)(size_t)&((AotFrame*)0)->stack;
    case 2: return (int)(size_t)&((AotFrame*)0)->sp;
    case 3: return (int)(size_t)&((AotFrame*)0)->exc_slot;
    case 4: return (int)(size_t)&((AotFrame*)0)->code_base;
    }
    return 0;
}
#define OFF_LOCALS   aot_off(0)
#define OFF_STACK    aot_off(1)
#define OFF_SP       aot_off(2)
#define OFF_EXCSLOT  aot_off(3)
#define OFF_CODEBASE aot_off(4)

// ============================================================
// 编译驱动（模板，后端 = 提供全部 static emit 函数的 struct）
// ============================================================
#ifdef AOT_MALLOC
extern "C" void* AOT_MALLOC(size_t);
extern "C" void AOT_FREE(void*);
static void* aot_drv_alloc(size_t n) { return AOT_MALLOC(n); }
static void aot_drv_free(void* p) { AOT_FREE(p); }
#else
#include "my/stdcompat.h"
static void* aot_drv_alloc(size_t n) { return malloc(n); }
static void aot_drv_free(void* p) { free(p); }
#endif

// 编译驱动：把 m 的字节码翻成机器码放进 E
template<typename B>
static bool aot_compile_generic(const uint8_t* code, uint32_t cl, Emit& E) {
    if (cl == 0 || cl > 65535) return false;

    uint32_t* bc_off = (uint32_t*)aot_drv_alloc((cl + 1) * 4);
    if (!bc_off) return false;
    for (uint32_t i = 0; i <= cl; i++) bc_off[i] = 0xffffffff;

    Fixup fix[MAX_FIX]; int nfix = 0;
    uint32_t divfix_at[MAX_DIV]; int ndiv = 0;

    B::prologue(E);

    uint32_t pc = 0;
    bool ok = true;
    while (pc < cl && ok && !E.fail) {
        uint32_t opaddr = pc;
        bc_off[pc] = E.len;
        uint8_t op = code[pc++];
        switch (op) {
        // ---------- constants ----------
        case 0x01: B::li_t0(E, 0); B::push_t0(E); break;               // aconst_null
        case 0x02: B::li_t0(E, -1); B::push_t0(E); break;              // iconst_m1
        case 0x03: case 0x04: case 0x05: case 0x06: case 0x07: case 0x08:
            B::li_t0(E, op - 3); B::push_t0(E); break;                 // iconst_0..5
        case 0x10: B::li_t0(E, (int8_t)code[pc]); pc++; B::push_t0(E); break;  // bipush
        case 0x11: {                                                    // sipush
            int16_t v = (int16_t)((code[pc] << 8) | code[pc+1]); pc += 2;
            B::li_t0(E, v); B::push_t0(E); break; }
        case 0x12: { uint8_t idx = code[pc++]; B::call_helper(E, H_LDC, idx, fix, nfix); break; }
        case 0x13: { uint16_t idx = (uint16_t)((code[pc] << 8) | code[pc+1]); pc += 2;
                     B::call_helper(E, H_LDC, idx, fix, nfix); break; }
        // ---------- loads ----------
        case 0x15: B::ld_local_t0(E, code[pc]); pc++; B::push_t0(E); break;    // iload
        case 0x19: B::ld_local_t0(E, code[pc]); pc++; B::push_t0(E); break;    // aload
        case 0x1a: case 0x1b: case 0x1c: case 0x1d:
            B::ld_local_t0(E, op - 0x1a); B::push_t0(E); break;
        case 0x2a: case 0x2b: case 0x2c: case 0x2d:
            B::ld_local_t0(E, op - 0x2a); B::push_t0(E); break;
        // ---------- stores ----------
        case 0x36: B::pop_t0(E); B::st_local_t0(E, code[pc]); pc++; break;     // istore
        case 0x3a: B::pop_t0(E); B::st_local_t0(E, code[pc]); pc++; break;     // astore
        case 0x3b: case 0x3c: case 0x3d: case 0x3e:
            B::pop_t0(E); B::st_local_t0(E, op - 0x3b); break;
        case 0x4b: case 0x4c: case 0x4d: case 0x4e:
            B::pop_t0(E); B::st_local_t0(E, op - 0x4b); break;
        // ---------- stack ----------
        case 0x57: B::pop_discard(E); break;                           // pop
        case 0x59: B::dup(E); break;
        case 0x5f: B::swap(E); break;
        // ---------- math ----------
        case 0x60: B::binop(E, B::OP_ADD); break;   // iadd
        case 0x64: B::binop(E, B::OP_SUB); break;   // isub
        case 0x68: B::binop(E, B::OP_MUL); break;   // imul
        case 0x6c: {                                                        // idiv
            if (ndiv >= MAX_DIV) { ok = false; break; }
            B::divop(E, false, divfix_at, ndiv, fix, nfix); ndiv++; break; }
        case 0x70: {                                                        // irem
            if (ndiv >= MAX_DIV) { ok = false; break; }
            B::divop(E, true, divfix_at, ndiv, fix, nfix); ndiv++; break; }
        case 0x74: B::neg(E); break;                                // ineg
        case 0x78: B::shiftop(E, B::OP_SHL); break;                 // ishl
        case 0x7a: B::shiftop(E, B::OP_SHR); break;                 // ishr
        case 0x7c: B::shiftop(E, B::OP_USHR); break;                // iushr
        case 0x7e: B::binop(E, B::OP_AND); break;                   // iand
        case 0x80: B::binop(E, B::OP_OR);  break;                   // ior
        case 0x82: B::binop(E, B::OP_XOR); break;                   // ixor
        case 0x84: {                                                        // iinc
            uint8_t n = code[pc++]; int8_t c = (int8_t)code[pc++];
            B::iinc(E, n, c); break; }
        case 0xc4: {                                                        // wide
            uint8_t sub = code[pc++];
            if (sub == 0x84) {                                              // wide iinc（javac 对 |增量|>127 会生成）
                uint16_t n = (uint16_t)((code[pc] << 8) | code[pc+1]); pc += 2;
                int16_t c = (int16_t)((code[pc] << 8) | code[pc+1]); pc += 2;
                B::iinc(E, n, c);
            } else if (sub == 0x15 || sub == 0x19) {                        // wide iload/aload
                uint16_t n = (uint16_t)((code[pc] << 8) | code[pc+1]); pc += 2;
                B::ld_local_t0(E, n); B::push_t0(E);
            } else if (sub == 0x36 || sub == 0x3a) {                        // wide istore/astore
                uint16_t n = (uint16_t)((code[pc] << 8) | code[pc+1]); pc += 2;
                B::pop_t0(E); B::st_local_t0(E, n);
            } else ok = false;
            break; }
        // ---------- conversions ----------
        case 0x91: B::pop_t0(E); B::sext_byte(E); B::push_t0(E); break;     // i2b
        case 0x92: B::pop_t0(E); B::zext_char(E); B::push_t0(E); break;     // i2c
        case 0x93: B::pop_t0(E); B::sext_short(E); B::push_t0(E); break;    // i2s
        // ---------- comparisons ----------
        case 0x9f: case 0xa0: case 0xa1: case 0xa2: case 0xa3: case 0xa4: {
            int16_t off = (int16_t)((code[pc] << 8) | code[pc+1]); pc += 2;
            if (!B::branch_cmp2(E, op, fix, nfix, opaddr + off)) { ok = false; }
            break; }
        case 0x99: case 0x9a: case 0x9b: case 0x9c: case 0x9d: case 0x9e: {
            int16_t off = (int16_t)((code[pc] << 8) | code[pc+1]); pc += 2;
            if (!B::branch_cmp1(E, op, fix, nfix, opaddr + off)) { ok = false; }
            break; }
        case 0xc6: {                                                        // ifnull
            int16_t off = (int16_t)((code[pc] << 8) | code[pc+1]); pc += 2;
            if (!B::branch_cmp1(E, 0x99, fix, nfix, opaddr + off)) { ok = false; }
            break; }
        case 0xc7: {                                                        // ifnonnull
            int16_t off = (int16_t)((code[pc] << 8) | code[pc+1]); pc += 2;
            if (!B::branch_cmp1(E, 0x9a, fix, nfix, opaddr + off)) { ok = false; }
            break; }
        case 0xa5: case 0xa6: {                                             // if_acmpeq/ne
            int16_t off = (int16_t)((code[pc] << 8) | code[pc+1]); pc += 2;
            uint8_t c2 = (op == 0xa5) ? 0x9f : 0xa0;
            if (!B::branch_cmp2(E, c2, fix, nfix, opaddr + off)) { ok = false; }
            break; }
        // ---------- control ----------
        case 0xa7: {                                                        // goto
            int16_t off = (int16_t)((code[pc] << 8) | code[pc+1]); pc += 2;
            if (!B::jump(E, fix, nfix, opaddr + off)) { ok = false; }
            break; }
        case 0xac: case 0xb0: B::ret_pop(E, fix, nfix); break;      // ireturn/areturn
        case 0xb1: B::ret_void(E, fix, nfix); break;                // return
        // ---------- references (helper) ----------
        case 0xb2: case 0xb3: case 0xb4: case 0xb5:
        case 0xb6: case 0xb7: case 0xb8: case 0xbb: case 0xbd: {
            uint16_t idx = (uint16_t)((code[pc] << 8) | code[pc+1]); pc += 2;
            int h = H_LDC;
            switch (op) {
            case 0xb2: h = H_GETSTATIC; break;
            case 0xb3: h = H_PUTSTATIC; break;
            case 0xb4: h = H_GETFIELD; break;
            case 0xb5: h = H_PUTFIELD; break;
            case 0xb6: h = H_INVOKEVIRTUAL; break;
            case 0xb7: h = H_INVOKESPECIAL; break;
            case 0xb8: h = H_INVOKESTATIC; break;
            case 0xbb: h = H_NEW; break;
            case 0xbd: h = H_ANEWARRAY; break;
            }
            B::call_helper(E, h, idx, fix, nfix); break; }
        case 0xbc: { uint8_t atype = code[pc++];
                     B::call_helper(E, H_NEWARRAY, atype, fix, nfix); break; }
        case 0xbe: B::call_helper(E, H_ARRAYLENGTH, 0, fix, nfix); break;
        case 0xbf: B::call_helper(E, H_ATHROW, 0, fix, nfix); break;
        // ---------- arrays (helper) ----------
        case 0x2e: B::call_helper(E, H_IALOAD, 0, fix, nfix); break;
        case 0x32: B::call_helper(E, H_AALOAD, 0, fix, nfix); break;
        case 0x33: B::call_helper(E, H_BALOAD, 0, fix, nfix); break;
        case 0x4f: B::call_helper(E, H_IASTORE, 0, fix, nfix); break;
        case 0x53: B::call_helper(E, H_AASTORE, 0, fix, nfix); break;
        case 0x54: B::call_helper(E, H_BASTORE, 0, fix, nfix); break;
        // ---------- misc ----------
        case 0xc2: case 0xc3: break;   // monitorenter/exit：单线程语义，忽略
        default:
            ok = false;   // 不支持的 opcode：整个方法回解释器
            break;
        }
    }

    // 除零慢路径：call throw_arith -> 跳异常出口
    uint32_t divstub[MAX_DIV];
    for (int i = 0; i < ndiv && ok; i++) {
        divstub[i] = E.len;
        B::call_helper_noexccheck(E, H_THROW_ARITH, fix, nfix);
    }

    // 出口
    uint32_t epi_throw_off = E.len;
    B::epilogue_throw(E);
    uint32_t epi_ret_off = E.len;
    B::epilogue_ret(E);

    // 回填
    for (int i = 0; i < nfix && ok; i++) {
        uint32_t target = 0;
        switch (fix[i].kind) {
        case FK_BC:
            if (fix[i].bc_target > cl || bc_off[fix[i].bc_target] == 0xffffffff) { ok = false; continue; }
            target = bc_off[fix[i].bc_target];
            break;
        case FK_EPI_RET:   target = epi_ret_off; break;
        case FK_EPI_THROW: target = epi_throw_off; break;
        case FK_DIVSTUB:   target = divstub[fix[i].stub_id]; break;
        }
        if (ok) B::patch_branch(E, fix[i].at, target);
    }

    aot_drv_free(bc_off);
    return ok && !E.fail;
}

