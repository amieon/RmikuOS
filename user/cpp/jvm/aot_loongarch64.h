// aot_loongarch64.h —— LoongArch64 后端（无 STL，可用于交叉测试）
//
// 寄存器分工（LoongArch 约定：$a0-a7 参数，$t0-t8 临时，$s0-s8 保存）：
//   $s0=fr  $s1=locals  $s2=stack  $s3=sp(字节偏移)  $s4=code_base
//   $t0/$t1/$t2 临时，$a0/$a1 helper 参数，$ra 调用
#pragma once
#include "aot_common.h"

struct LoongArch64 {
    enum { X0=0, RA=1, SP=3, A0=4, A1=5, T0=12, T1=13, T2=14,
           FR=23, L=24, ST=25, S3=26, CB=27 };

    enum { OP_ADD=0, OP_SUB, OP_MUL, OP_AND, OP_OR, OP_XOR,
           OP_SHL, OP_SHR, OP_USHR };

    // ---- 基础编码 ----
    // 3R 型：[31:15]=op, [14:10]=rk, [9:5]=rj, [4:0]=rd
    static void r3(Emit& e, uint32_t op, int rk, int rj, int rd) {
        e.u32((op << 15) | ((uint32_t)rk << 10) | ((uint32_t)rj << 5) | (uint32_t)rd);
    }
    // 2RI12 型：[31:22]=op, [21:10]=imm12, [9:5]=rj, [4:0]=rd
    static void ri12(Emit& e, uint32_t op, int imm, int rj, int rd) {
        e.u32((op << 22) | (((uint32_t)imm & 0xfff) << 10) |
              ((uint32_t)rj << 5) | (uint32_t)rd);
    }
    // 1RI20 型：[31:25]=op, [24:5]=si20, [4:0]=rd
    static void ri20(Emit& e, uint32_t op, int imm20, int rd) {
        e.u32((op << 25) | (((uint32_t)imm20 & 0xfffff) << 5) | (uint32_t)rd);
    }

    static void add_d(Emit& e, int rd, int rj, int rk)  { r3(e, 0x00021, rk, rj, rd); }
    static void sub_w(Emit& e, int rd, int rj, int rk)  { r3(e, 0x00022, rk, rj, rd); }
    static void addi_d(Emit& e, int rd, int rj, int imm){ ri12(e, 0x00B, imm, rj, rd); }
    static void andi(Emit& e, int rd, int rj, int imm)  { ri12(e, 0x00D, imm, rj, rd); }
    static void ori(Emit& e, int rd, int rj, int imm)   { ri12(e, 0x00E, imm, rj, rd); }
    static void lu12i_w(Emit& e, int rd, int imm20)     { ri20(e, 0x0A, imm20, rd); }
    // ld.d/st.d：imm 就是字节偏移（si12，不缩放）
    static void ld_d(Emit& e, int rd, int rj, int off)  { ri12(e, 0x0A3, off, rj, rd); }
    static void st_d(Emit& e, int rd, int rj, int off)  { ri12(e, 0x0A7, off, rj, rd); }
    static void slli_d(Emit& e, int rd, int rj, int sh) {
        e.u32((0x0041u << 16) | (((uint32_t)sh & 0x3f) << 10) | ((uint32_t)rj << 5) | (uint32_t)rd);
    }
    static void srli_d(Emit& e, int rd, int rj, int sh) {
        e.u32((0x0045u << 16) | (((uint32_t)sh & 0x3f) << 10) | ((uint32_t)rj << 5) | (uint32_t)rd);
    }
    static void srai_d(Emit& e, int rd, int rj, int sh) {
        e.u32((0x0049u << 16) | (((uint32_t)sh & 0x3f) << 10) | ((uint32_t)rj << 5) | (uint32_t)rd);
    }
    // 2RI16 分支占位：[31:26]=op, [25:10]=si16, [9:5]=rj, [4:0]=rd
    static uint32_t branch(Emit& e, uint32_t op, int rj, int rd) {
        uint32_t at = e.len;
        e.u32((op << 26) | ((uint32_t)rj << 5) | (uint32_t)rd);
        return at;
    }
    static void patch_branch(Emit& e, uint32_t at, uint32_t target) {
        int32_t rel = (int32_t)target - (int32_t)at;
        uint32_t base = e.read32(at);
        e.patch32(at, base | (((uint32_t)(rel >> 2) & 0xffff) << 10));
    }
    static void jirl(Emit& e, int rd, int rj, int imm) {
        e.u32((0x13u << 26) | (((uint32_t)(imm >> 2) & 0xffff) << 10) |
              ((uint32_t)rj << 5) | (uint32_t)rd);
    }
    static void li32(Emit& e, int rd, int32_t v) {
        if (v >= -2048 && v <= 2047) { addi_d(e, rd, X0, v); return; }
        int32_t hi = (v >> 12) & 0xfffff;
        lu12i_w(e, rd, hi);
        ori(e, rd, rd, (uint32_t)v & 0xfff);
    }

    // ---- 栈槽操作 ----
    static void push_t0(Emit& e) {
        add_d(e, T2, ST, S3); st_d(e, T0, T2, 0); addi_d(e, S3, S3, 8);
    }
    static void pop_t0(Emit& e) {
        addi_d(e, S3, S3, -8); add_d(e, T2, ST, S3); ld_d(e, T0, T2, 0);
    }
    static void pop_t1(Emit& e) {
        addi_d(e, S3, S3, -8); add_d(e, T2, ST, S3); ld_d(e, T1, T2, 0);
    }
    static void pop_discard(Emit& e) { addi_d(e, S3, S3, -8); }
    static void dup(Emit& e) {
        add_d(e, T2, ST, S3); ld_d(e, T0, T2, -8); st_d(e, T0, T2, 0); addi_d(e, S3, S3, 8);
    }
    static void swap(Emit& e) {
        add_d(e, T2, ST, S3);
        ld_d(e, T0, T2, -16); ld_d(e, T1, T2, -8);
        st_d(e, T0, T2, -8);  st_d(e, T1, T2, -16);
    }

    // ---- locals ----
    static void ld_local_t0(Emit& e, int n) { ld_d(e, T0, L, 8 * n); }
    static void st_local_t0(Emit& e, int n) { st_d(e, T0, L, 8 * n); }

    // ---- 常量 ----
    static void li_t0(Emit& e, int32_t v) { li32(e, T0, v); }

    // ---- 算术 ----
    static void binop(Emit& e, int op) {
        pop_t1(e); pop_t0(e);
        switch (op) {
        case OP_ADD: r3(e, 0x00020, T1, T0, T0); break;  // add.w
        case OP_SUB: sub_w(e, T0, T0, T1); break;
        case OP_MUL: r3(e, 0x00038, T1, T0, T0); break;  // mul.w
        case OP_AND: r3(e, 0x00029, T1, T0, T0); break;  // and
        case OP_OR:  r3(e, 0x0002A, T1, T0, T0); break;  // or
        case OP_XOR: r3(e, 0x0002B, T1, T0, T0); break;  // xor
        }
        push_t0(e);
    }
    static void divop(Emit& e, bool is_rem, uint32_t* divfix_at, int ndiv,
                      Fixup* fix, int& nfix) {
        (void)divfix_at;
        pop_t1(e); pop_t0(e);
        uint32_t at = branch(e, 0x16, T1, X0);       // beq t1, zero -> 除零慢路径
        aot_add_fix(fix, nfix, at, 0, FK_DIVSTUB, (uint32_t)ndiv);
        if (is_rem) r3(e, 0x00041, T1, T0, T0);      // mod.w
        else        r3(e, 0x00040, T1, T0, T0);      // div.w
        push_t0(e);
    }
    static void neg(Emit& e) {
        pop_t0(e); sub_w(e, T0, X0, T0); push_t0(e);
    }
    static void shiftop(Emit& e, int op) {
        pop_t1(e); pop_t0(e);
        andi(e, T1, T1, 31);
        switch (op) {
        case OP_SHL:  r3(e, 0x0002E, T1, T0, T0); break;  // sll.w
        case OP_SHR:  r3(e, 0x00030, T1, T0, T0); break;  // sra.w
        case OP_USHR: r3(e, 0x0002F, T1, T0, T0); break;  // srl.w
        }
        push_t0(e);
    }
    static void iinc(Emit& e, int n, int c) {
        ld_d(e, T0, L, 8 * n);
        if (c >= -2048 && c <= 2047) {
            addi_d(e, T0, T0, c);
        } else {  // wide iinc：常量超 addi.d 的 12 位立即数范围
            li32(e, T1, c); add_d(e, T0, T0, T1);
        }
        st_d(e, T0, L, 8 * n);
    }
    static void sext_byte(Emit& e)  { slli_d(e, T0, T0, 56); srai_d(e, T0, T0, 56); }
    static void zext_char(Emit& e)  { slli_d(e, T0, T0, 48); srli_d(e, T0, T0, 48); }
    static void sext_short(Emit& e) { slli_d(e, T0, T0, 48); srai_d(e, T0, T0, 48); }

    // ---- 分支 ----
    static bool branch_cmp2(Emit& e, uint8_t op, Fixup* fix, int& nfix, uint32_t target) {
        pop_t1(e); pop_t0(e);
        uint32_t at;
        switch (op) {
        case 0x9f: at = branch(e, 0x16, T0, T1); break;  // beq
        case 0xa0: at = branch(e, 0x17, T0, T1); break;  // bne
        case 0xa1: at = branch(e, 0x18, T0, T1); break;  // blt
        case 0xa2: at = branch(e, 0x19, T0, T1); break;  // bge
        case 0xa3: at = branch(e, 0x18, T1, T0); break;  // gt -> blt t1,t0
        default:   at = branch(e, 0x19, T1, T0); break;  // le -> bge t1,t0
        }
        return aot_add_fix(fix, nfix, at, target, FK_BC);
    }
    static bool branch_cmp1(Emit& e, uint8_t op, Fixup* fix, int& nfix, uint32_t target) {
        pop_t0(e);
        uint32_t at;
        switch (op) {
        case 0x99: at = branch(e, 0x16, T0, X0); break;  // eq
        case 0x9a: at = branch(e, 0x17, T0, X0); break;  // ne
        case 0x9b: at = branch(e, 0x18, T0, X0); break;  // lt
        case 0x9c: at = branch(e, 0x19, T0, X0); break;  // ge
        case 0x9d: at = branch(e, 0x18, X0, T0); break;  // gt
        default:   at = branch(e, 0x19, X0, T0); break;  // le
        }
        return aot_add_fix(fix, nfix, at, target, FK_BC);
    }
    static bool jump(Emit& e, Fixup* fix, int& nfix, uint32_t target) {
        uint32_t at = branch(e, 0x16, X0, X0);
        return aot_add_fix(fix, nfix, at, target, FK_BC);
    }

    // ---- 返回 ----
    static void ret_pop(Emit& e, Fixup* fix, int& nfix) {
        pop_t0(e);
        addi_d(e, A0, T0, 0);
        uint32_t at = branch(e, 0x16, X0, X0);
        aot_add_fix(fix, nfix, at, 0, FK_EPI_RET);
    }
    static void ret_void(Emit& e, Fixup* fix, int& nfix) {
        addi_d(e, A0, X0, 0);
        uint32_t at = branch(e, 0x16, X0, X0);
        aot_add_fix(fix, nfix, at, 0, FK_EPI_RET);
    }

    // ---- helper 调用 ----
    static void call_helper(Emit& e, int k, uint32_t imm, Fixup* fix, int& nfix) {
        st_d(e, S3, FR, OFF_SP);
        addi_d(e, A0, FR, 0);
        li32(e, A1, (int32_t)imm);
        ld_d(e, T2, CB, (int32_t)(k * 8) - (int32_t)TABLE_BYTES);
        jirl(e, RA, T2, 0);
        ld_d(e, S3, FR, OFF_SP);
        ld_d(e, T0, FR, OFF_EXCSLOT);
        ld_d(e, T0, T0, 0);
        uint32_t at = branch(e, 0x17, T0, X0);       // bne t0, zero -> 异常出口
        aot_add_fix(fix, nfix, at, 0, FK_EPI_THROW);
    }
    static void call_helper_noexccheck(Emit& e, int k, Fixup* fix, int& nfix) {
        st_d(e, S3, FR, OFF_SP);
        addi_d(e, A0, FR, 0);
        addi_d(e, A1, X0, 0);
        ld_d(e, T2, CB, (int32_t)(k * 8) - (int32_t)TABLE_BYTES);
        jirl(e, RA, T2, 0);
        uint32_t at = branch(e, 0x16, X0, X0);
        aot_add_fix(fix, nfix, at, 0, FK_EPI_THROW);
    }

    // ---- 帧结构 ----
    static void prologue(Emit& e) {
        addi_d(e, SP, SP, -64);
        st_d(e, RA, SP, 56);
        st_d(e, FR, SP, 48);
        st_d(e, L,  SP, 40);
        st_d(e, ST, SP, 32);
        st_d(e, S3, SP, 24);
        st_d(e, CB, SP, 16);
        addi_d(e, FR, A0, 0);
        ld_d(e, L,  FR, OFF_LOCALS);
        ld_d(e, ST, FR, OFF_STACK);
        addi_d(e, S3, X0, 0);
        ld_d(e, CB, FR, OFF_CODEBASE);
    }
    static void epilogue_throw(Emit& e) { addi_d(e, A0, X0, 0); }
    static void epilogue_ret(Emit& e) {
        ld_d(e, RA, SP, 56);
        ld_d(e, FR, SP, 48);
        ld_d(e, L,  SP, 40);
        ld_d(e, ST, SP, 32);
        ld_d(e, S3, SP, 24);
        ld_d(e, CB, SP, 16);
        addi_d(e, SP, SP, 64);
        jirl(e, X0, RA, 0);
    }

    static void flush_icache(void* p, size_t n) {
        (void)p; (void)n;
#if defined(__loongarch) || defined(__loongarch__)
        asm volatile("ibar 0" ::: "memory");
#endif
    }
};