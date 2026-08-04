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
int setjmp(jmp_buf env) {
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
void longjmp(jmp_buf env, int val) {
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

/* 布局与 glibc sysdeps/loongarch 的 __sigsetjmp 保持一致:
 *   0=ra  8=sp  16=r21(ABI 保留)  24=fp
 *   32..96=s0..s8(9 个)
 *   104..160=f24..f31(8 个浮点 callee-saved, LP64D)
 * 浮点 callee-saved(f24-f31)必须保存, 否则 longjmp 后 Lua 等
 * 浮点程序的 double 状态全部损坏。
 *
 * 注意: 严禁用 static __attribute__((naked)) 在头文件里实现!
 * loongarch64-unknown-linux-gnu-gcc(>= 15)对 naked 支持有 bug:
 * 它会给函数生成 prologue(sp-=32、fp 指向自己的帧)却在返回时
 * 绕过 epilogue 直接 ret, 导致 setjmp 返回后调用方 $fp 被污染、
 * 且保存进 env 的 sp/fp 也是 setjmp 自己的错误值。
 * 纯汇编实现在 user/lib/syscall_loongarch64.S(随 _syscall.o
 * 链接进所有 C 项目)。 */
typedef struct { unsigned long buf[21]; } jmp_buf[1];

int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);

#else
#  error "setjmp.h: unsupported architecture (need __riscv or __loongarch__)"
#endif

#endif