#pragma once
#ifndef SETJMP_H
#define SETJMP_H

/*
 * 一份头文件，RISC-V / LoongArch 双架构。
 * 风格参考 mm_sqrt 的 #if 条件编译。
 *
 * 共同约束：
 *   naked    —— 禁止 prologue/epilogue，否则保存的 sp 是调整后的值。
 *   noinline —— setjmp 必须是真实调用，jal/bl 才会把
 *               "setjmp 调用点之后"的地址写进 ra。
 *   unused   —— 头文件里 static 定义，没被用到的翻译单元不报警告。
 *   纯 asm + ABI 寄存器名，符合 GCC 对 naked 函数的要求
 *   （naked 函数里不要用扩展 asm 操作数 / clobber 寄存器）。
 *
 * 寄存器名：RISC-V 不带 $（a0,sp,ra...），LoongArch 带 $（$a0,$sp,$ra...）。
 */

#if defined(__riscv)

typedef struct { unsigned long buf[14]; } jmp_buf[1];  /* s0-s11 + sp + ra */

static __attribute__((naked, noinline, unused))
static inline int setjmp(jmp_buf env) {
    __asm__ volatile (
        "sd s0,    0(a0)\n\t"
        "sd s1,    8(a0)\n\t"
        "sd s2,   16(a0)\n\t"
        "sd s3,   24(a0)\n\t"
        "sd s4,   32(a0)\n\t"
        "sd s5,   40(a0)\n\t"
        "sd s6,   48(a0)\n\t"
        "sd s7,   56(a0)\n\t"
        "sd s8,   64(a0)\n\t"
        "sd s9,   72(a0)\n\t"
        "sd s10,  80(a0)\n\t"
        "sd s11,  88(a0)\n\t"
        "sd sp,   96(a0)\n\t"
        "sd ra,  104(a0)\n\t"
        "li a0, 0\n\t"
        "ret"
    );
}

static __attribute__((naked, noinline, unused))
static inline void longjmp(jmp_buf env, int val) {
    __asm__ volatile (
        "ld s0,    0(a0)\n\t"
        "ld s1,    8(a0)\n\t"
        "ld s2,   16(a0)\n\t"
        "ld s3,   24(a0)\n\t"
        "ld s4,   32(a0)\n\t"
        "ld s5,   40(a0)\n\t"
        "ld s6,   48(a0)\n\t"
        "ld s7,   56(a0)\n\t"
        "ld s8,   64(a0)\n\t"
        "ld s9,   72(a0)\n\t"
        "ld s10,  80(a0)\n\t"
        "ld s11,  88(a0)\n\t"
        "ld sp,   96(a0)\n\t"
        "ld ra,  104(a0)\n\t"
        "seqz a0, a1\n\t"
        "add  a0, a0, a1\n\t"
        "ret"
    );
}

#elif defined(__loongarch__) || defined(__loongarch64__) || defined(__loongarch64)

typedef struct { unsigned long buf[12]; } jmp_buf[1];  /* ra,sp,fp,s0-s8 */

/* 布局(字节偏移): 0=ra 8=sp 16=fp 24..88=s0..s8
 * 浮点寄存器全是 caller-saved，不存。 */

static __attribute__((naked, noinline, unused))
static inline int setjmp(jmp_buf env) {           /* env 在 $a0 */
    __asm__ volatile (
        "st.d $ra, $a0, 0\n\t"
        "st.d $sp, $a0, 8\n\t"
        "st.d $fp, $a0, 16\n\t"
        "st.d $s0, $a0, 24\n\t"
        "st.d $s1, $a0, 32\n\t"
        "st.d $s2, $a0, 40\n\t"
        "st.d $s3, $a0, 48\n\t"
        "st.d $s4, $a0, 56\n\t"
        "st.d $s5, $a0, 64\n\t"
        "st.d $s6, $a0, 72\n\t"
        "st.d $s7, $a0, 80\n\t"
        "st.d $s8, $a0, 88\n\t"
        "addi.d $a0, $zero, 0\n\t"  /* 返回值 0（覆盖已用完的基址 $a0） */
        "jirl $zero, $ra, 0"        /* 用未改动的 $ra 返回调用点 */
    );
}

static __attribute__((naked, noinline, unused))
static inline void longjmp(jmp_buf env, int val) { /* env 在 $a0, val 在 $a1 */
    __asm__ volatile (
        "ld.d $s0, $a0, 24\n\t"
        "ld.d $s1, $a0, 32\n\t"
        "ld.d $s2, $a0, 40\n\t"
        "ld.d $s3, $a0, 48\n\t"
        "ld.d $s4, $a0, 56\n\t"
        "ld.d $s5, $a0, 64\n\t"
        "ld.d $s6, $a0, 72\n\t"
        "ld.d $s7, $a0, 80\n\t"
        "ld.d $s8, $a0, 88\n\t"
        "ld.d $fp, $a0, 16\n\t"
        "ld.d $sp, $a0, 8\n\t"      /* 此处栈已切走，但下面只动寄存器、不访旧栈 */
        "ld.d $ra, $a0, 0\n\t"      /* 基址仍是 $a0(env)，与 sp 无关，安全 */
        "sltui $a0, $a1, 1\n\t"     /* $a0 = (val == 0) ? 1 : 0 */
        "add.d $a0, $a0, $a1\n\t"   /* 合成：val==0 -> 1，否则 -> val */
        "jirl $zero, $ra, 0"        /* 用刚恢复的 $ra 跳回 setjmp 调用点 */
    );
}

#else
#  error "setjmp.h: unsupported architecture (need __riscv or __loongarch__)"
#endif

#endif