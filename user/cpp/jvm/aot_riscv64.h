// aot_riscv64.h —— RISC-V64 后端（无 STL，可用于交叉测试）
#pragma once
#include "aot_common.h"

// ============================================================
// 第 4 部分：RISC-V64 后端
// 寄存器分工：s0=fr  s1=locals  s2=stack  s3=sp(字节偏移)  s5=code_base
//             t0/t1/t2 临时，a0/a1 helper 参数，ra 调用
// ============================================================

struct Riscv64 {
    enum { X0=0, RA=1, SP=2, T0=5, T1=6, T2=7,
           FR=8, L=9, A0=10, A1=11, ST=18, S3=19, CB=21 };

    enum { OP_ADD=0, OP_SUB, OP_MUL, OP_AND, OP_OR, OP_XOR,
           OP_SHL, OP_SHR, OP_USHR };

    // ---- 基础编码 ----
    static void rtype(Emit& e, int f7, int rs2, int rs1, int f3, int rd, int op) {
        e.u32(((uint32_t)f7 << 25) | ((uint32_t)rs2 << 20) | ((uint32_t)rs1 << 15) |
              ((uint32_t)f3 << 12) | ((uint32_t)rd << 7) | (uint32_t)op);
    }
    static void itype(Emit& e, int imm, int rs1, int f3, int rd, int op) {
        e.u32(((uint32_t)(imm & 0xfff) << 20) | ((uint32_t)rs1 << 15) |
              ((uint32_t)f3 << 12) | ((uint32_t)rd << 7) | (uint32_t)op);
    }
    static void stype(Emit& e, int imm, int rs2, int rs1, int f3, int op) {
        e.u32((((uint32_t)(imm >> 5) & 0x7f) << 25) | ((uint32_t)rs2 << 20) |
              ((uint32_t)rs1 << 15) | ((uint32_t)f3 << 12) |
              (((uint32_t)imm & 0x1f) << 7) | (uint32_t)op);
    }
    static void addi(Emit& e, int rd, int rs1, int imm)  { itype(e, imm, rs1, 0, rd, 0x13); }
    static void addiw(Emit& e, int rd, int rs1, int imm) { itype(e, imm, rs1, 0, rd, 0x1b); }
    static void andi(Emit& e, int rd, int rs1, int imm)  { itype(e, imm, rs1, 7, rd, 0x13); }
    static void ld(Emit& e, int rd, int rs1, int imm)    { itype(e, imm, rs1, 3, rd, 0x03); }
    static void sd(Emit& e, int rs2, int rs1, int imm)   { stype(e, imm, rs2, rs1, 3, 0x23); }
    static void slli(Emit& e, int rd, int rs1, int sh)   { itype(e, sh & 0x3f, rs1, 1, rd, 0x13); }
    static void srli(Emit& e, int rd, int rs1, int sh)   { itype(e, sh & 0x3f, rs1, 5, rd, 0x13); }
    static void srai(Emit& e, int rd, int rs1, int sh)   { itype(e, (sh & 0x3f) | 0x400, rs1, 5, rd, 0x13); }
    static void add(Emit& e, int rd, int r1, int r2)     { rtype(e, 0, r2, r1, 0, rd, 0x33); }
    static void lui(Emit& e, int rd, int imm20)          { e.u32(((uint32_t)imm20 << 12) | ((uint32_t)rd << 7) | 0x37); }
    static void jalr(Emit& e, int rd, int rs1, int imm)  { itype(e, imm, rs1, 0, rd, 0x67); }
    // 占位跳转：B-type 条件分支只有 ±4KB 范围，大方法（如 traceRay 8KB）会溢出回绕。
    // 改为「反转条件分支 +8 跳过 / JAL 占位」双槽序列（JAL J-type ±1MB，必然可达），
    // patch_branch 回填 JAL 槽；返回的 fixup 偏移指向 JAL 槽。
    static uint32_t branch(Emit& e, int f3, int rs1, int rs2) {
        if (f3 == 0 && rs1 == X0 && rs2 == X0) {      // beq x0,x0：无条件跳，直接 JAL
            uint32_t at = e.len;
            e.u32(0x6f);                             // jal x0, 0（占位）
            return at;
        }
        // 条件跳：b<f3^1> rs1,rs2,+8（跳过 JAL）/ jal x0, target
        // f3^1：beq<->bne、blt<->bge；+8 的 B-type 编码即 imm[4:1]=4 -> 位[11:8]
        e.u32(((uint32_t)rs2 << 20) | ((uint32_t)rs1 << 15) |
              ((uint32_t)(f3 ^ 1) << 12) | (4u << 8) | 0x63);
        uint32_t at = e.len;
        e.u32(0x6f);                                 // jal x0, 0（占位）
        return at;
    }
    static void patch_branch(Emit& e, uint32_t at, uint32_t target) {
        int32_t rel = (int32_t)target - (int32_t)at;  // JAL ±1MB；缓冲 16KB 内不会溢出
        uint32_t imm = (((uint32_t)(rel >> 1)  & 0x3ff) << 21) |  // imm[10:1]
                       (((uint32_t)(rel >> 11) & 0x1)   << 20) |  // imm[11]
                       (((uint32_t)(rel >> 12) & 0xff)  << 12) |  // imm[19:12]
                       (((uint32_t)(rel >> 20) & 0x1)   << 31);   // imm[20]
        e.patch32(at, imm | 0x6f);                   // rd = x0
    }
    static void li32(Emit& e, int rd, int32_t v) {
        if (v >= -2048 && v <= 2047) { addi(e, rd, X0, v); return; }
        int32_t hi = (v + 0x800) >> 12;
        int32_t lo = v - (hi << 12);
        lui(e, rd, hi & 0xfffff);
        addiw(e, rd, rd, lo);
    }

    // ---- 栈槽操作 ----
    static void push_t0(Emit& e) {
        add(e, T2, ST, S3); sd(e, T0, T2, 0); addi(e, S3, S3, 8);
    }
    static void pop_t0(Emit& e) {
        addi(e, S3, S3, -8); add(e, T2, ST, S3); ld(e, T0, T2, 0);
    }
    static void pop_t1(Emit& e) {
        addi(e, S3, S3, -8); add(e, T2, ST, S3); ld(e, T1, T2, 0);
    }
    static void pop_discard(Emit& e) { addi(e, S3, S3, -8); }
    static void dup(Emit& e) {
        add(e, T2, ST, S3); ld(e, T0, T2, -8); sd(e, T0, T2, 0); addi(e, S3, S3, 8);
    }
    static void swap(Emit& e) {
        add(e, T2, ST, S3);
        ld(e, T0, T2, -16); ld(e, T1, T2, -8);
        sd(e, T0, T2, -8);  sd(e, T1, T2, -16);
    }

    // ---- locals ----
    static void ld_local_t0(Emit& e, int n) { ld(e, T0, L, 8 * n); }
    static void st_local_t0(Emit& e, int n) { sd(e, T0, L, 8 * n); }

    // ---- 常量 ----
    static void li_t0(Emit& e, int32_t v) { li32(e, T0, v); }

    // ---- 算术 ----
    static void binop(Emit& e, int op) {
        pop_t1(e); pop_t0(e);
        switch (op) {
        case OP_ADD: rtype(e, 0x00, T1, T0, 0, T0, 0x3b); break;  // addw
        case OP_SUB: rtype(e, 0x20, T1, T0, 0, T0, 0x3b); break;  // subw
        case OP_MUL: rtype(e, 0x01, T1, T0, 0, T0, 0x3b); break;  // mulw
        case OP_AND: rtype(e, 0x00, T1, T0, 7, T0, 0x33); break;  // and
        case OP_OR:  rtype(e, 0x00, T1, T0, 6, T0, 0x33); break;  // or
        case OP_XOR: rtype(e, 0x00, T1, T0, 4, T0, 0x33); break;  // xor
        }
        push_t0(e);
    }
    static void divop(Emit& e, bool is_rem, uint32_t* divfix_at, int ndiv,
                      Fixup* fix, int& nfix) {
        (void)divfix_at;
        pop_t1(e); pop_t0(e);
        uint32_t at = branch(e, 0, T1, X0);          // beqz T1 -> 除零慢路径
        aot_add_fix(fix, nfix, at, 0, FK_DIVSTUB, (uint32_t)ndiv);
        if (is_rem) rtype(e, 0x01, T1, T0, 6, T0, 0x3b);  // remw
        else        rtype(e, 0x01, T1, T0, 4, T0, 0x3b);  // divw
        push_t0(e);
    }
    static void neg(Emit& e) {
        pop_t0(e); rtype(e, 0x20, T0, X0, 0, T0, 0x3b); push_t0(e);  // subw t0, x0, t0
    }
    static void shiftop(Emit& e, int op) {
        pop_t1(e); pop_t0(e);
        andi(e, T1, T1, 31);
        switch (op) {
        case OP_SHL:  rtype(e, 0x00, T1, T0, 1, T0, 0x3b); break;  // sllw
        case OP_SHR:  rtype(e, 0x20, T1, T0, 5, T0, 0x3b); break;  // sraw
        case OP_USHR: rtype(e, 0x00, T1, T0, 5, T0, 0x3b); break;  // srlw
        }
        push_t0(e);
    }
    static void iinc(Emit& e, int n, int c) {
        ld(e, T0, L, 8 * n);
        if (c >= -2048 && c <= 2047) {
            addi(e, T0, T0, c);
        } else {  // wide iinc：常量超 addi 的 12 位立即数范围
            li32(e, T1, c); add(e, T0, T0, T1);
        }
        sd(e, T0, L, 8 * n);
    }
    static void sext_byte(Emit& e)  { slli(e, T0, T0, 56); srai(e, T0, T0, 56); }
    static void zext_char(Emit& e)  { slli(e, T0, T0, 48); srli(e, T0, T0, 48); }
    static void sext_short(Emit& e) { slli(e, T0, T0, 48); srai(e, T0, T0, 48); }

    // ---- 分支 ----
    static bool branch_cmp2(Emit& e, uint8_t op, Fixup* fix, int& nfix, uint32_t target) {
        pop_t1(e); pop_t0(e);
        uint32_t at;
        switch (op) {
        case 0x9f: at = branch(e, 0, T0, T1); break;  // beq
        case 0xa0: at = branch(e, 1, T0, T1); break;  // bne
        case 0xa1: at = branch(e, 4, T0, T1); break;  // blt
        case 0xa2: at = branch(e, 5, T0, T1); break;  // bge
        case 0xa3: at = branch(e, 4, T1, T0); break;  // gt -> blt t1,t0
        default:   at = branch(e, 5, T1, T0); break;  // le -> bge t1,t0
        }
        return aot_add_fix(fix, nfix, at, target, FK_BC);
    }
    static bool branch_cmp1(Emit& e, uint8_t op, Fixup* fix, int& nfix, uint32_t target) {
        pop_t0(e);
        uint32_t at;
        switch (op) {
        case 0x99: at = branch(e, 0, T0, X0); break;  // eq
        case 0x9a: at = branch(e, 1, T0, X0); break;  // ne
        case 0x9b: at = branch(e, 4, T0, X0); break;  // lt
        case 0x9c: at = branch(e, 5, T0, X0); break;  // ge
        case 0x9d: at = branch(e, 4, X0, T0); break;  // gt
        default:   at = branch(e, 5, X0, T0); break;  // le
        }
        return aot_add_fix(fix, nfix, at, target, FK_BC);
    }
    static bool jump(Emit& e, Fixup* fix, int& nfix, uint32_t target) {
        uint32_t at = branch(e, 0, X0, X0);           // beq x0,x0 无条件跳
        return aot_add_fix(fix, nfix, at, target, FK_BC);
    }

    // ---- 返回 ----
    static void ret_pop(Emit& e, Fixup* fix, int& nfix) {
        pop_t0(e);
        addi(e, A0, T0, 0);
        uint32_t at = branch(e, 0, X0, X0);
        aot_add_fix(fix, nfix, at, 0, FK_EPI_RET);
    }
    static void ret_void(Emit& e, Fixup* fix, int& nfix) {
        addi(e, A0, X0, 0);
        uint32_t at = branch(e, 0, X0, X0);
        aot_add_fix(fix, nfix, at, 0, FK_EPI_RET);
    }

    // ---- helper 调用 ----
    static void call_helper(Emit& e, int k, uint32_t imm, Fixup* fix, int& nfix) {
        sd(e, S3, FR, OFF_SP);                       // 同步 sp
        addi(e, A0, FR, 0);
        li32(e, A1, (int32_t)imm);
        ld(e, RA, CB, (int32_t)(k * 8) - (int32_t)TABLE_BYTES);
        jalr(e, RA, RA, 0);
        ld(e, S3, FR, OFF_SP);                       // 恢复 sp
        ld(e, T0, FR, OFF_EXCSLOT);                  // 异常检查
        ld(e, T0, T0, 0);
        uint32_t at = branch(e, 1, T0, X0);          // bnez -> 异常出口
        aot_add_fix(fix, nfix, at, 0, FK_EPI_THROW);
    }
    static void call_helper_noexccheck(Emit& e, int k, Fixup* fix, int& nfix) {
        sd(e, S3, FR, OFF_SP);
        addi(e, A0, FR, 0);
        addi(e, A1, X0, 0);
        ld(e, RA, CB, (int32_t)(k * 8) - (int32_t)TABLE_BYTES);
        jalr(e, RA, RA, 0);
        uint32_t at = branch(e, 0, X0, X0);          // 完成后直接走异常出口
        aot_add_fix(fix, nfix, at, 0, FK_EPI_THROW);
    }

    // ---- 帧结构 ----
    static void prologue(Emit& e) {
        addi(e, SP, SP, -64);
        sd(e, RA, SP, 56);
        sd(e, FR, SP, 48);
        sd(e, L,  SP, 40);
        sd(e, ST, SP, 32);
        sd(e, S3, SP, 24);
        sd(e, CB, SP, 16);
        addi(e, FR, A0, 0);
        ld(e, L,  FR, OFF_LOCALS);
        ld(e, ST, FR, OFF_STACK);
        addi(e, S3, X0, 0);
        ld(e, CB, FR, OFF_CODEBASE);
    }
    static void epilogue_throw(Emit& e) { addi(e, A0, X0, 0); }
    static void epilogue_ret(Emit& e) {
        ld(e, RA, SP, 56);
        ld(e, FR, SP, 48);
        ld(e, L,  SP, 40);
        ld(e, ST, SP, 32);
        ld(e, S3, SP, 24);
        ld(e, CB, SP, 16);
        addi(e, SP, SP, 64);
        jalr(e, X0, RA, 0);
    }

    static void flush_icache(void* p, size_t n) {
        (void)p; (void)n;
#if defined(__riscv) && (__riscv_xlen == 64)
        asm volatile("fence.i" ::: "memory");
#endif
    }
};