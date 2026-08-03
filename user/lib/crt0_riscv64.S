    .section .text.entry
    .global _start
    .type _start, @function

_start:
    # 保存 envp(a2) 到全局 environ(POSIX): exec 时内核在 TrapContext
    # 设 a0=argc a1=argv a2=envp, crt0 直接透传给 main。
    la      t0, environ
    sd      a2, 0(t0)

    call main

    /* exit(code): 冲刷标准流缓冲后 syscall 退出(短输出无换行不丢) */
    call exit
1:
    j 1b

    .section .data
    .global environ
    .type environ, @object
environ:
    .quad 0
    .size environ, 8