    .section .text.entry
    .global _start
    .type _start, @function

_start:
    # 保存 envp(a2) 到全局 environ(POSIX): exec 时内核在 TrapContext
    # 设 a0=argc a1=argv a2=envp, crt0 直接透传给 main。
    la      t0, environ
    sd      a2, 0(t0)

    /* C++ 全局对象构造: 正序遍历 .init_array。
     * 用 s0/s1(callee-saved), ctor 不会破坏循环变量; t0-t2 会被 ctor 改。 */
    la      s0, __init_array_start
    la      s1, __init_array_end
.Linit_loop:
    bgeu    s0, s1, .Linit_done
    ld      t2, 0(s0)
    addi    s0, s0, 8
    jalr    ra, 0(t2)
    j       .Linit_loop
.Linit_done:

    call main

    /* exit(code): 跑 C++ 析构 + 冲刷标准流缓冲后 syscall 退出 */
    call exit
.Lhang:
    j .Lhang

    .section .data
    .global environ
    .type environ, @object
environ:
    .quad 0
    .size environ, 8
