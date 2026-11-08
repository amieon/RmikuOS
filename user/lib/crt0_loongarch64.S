    .section .text.entry
    .global _start
    .type _start, @function

_start:
    # 保存 envp(a2) 到全局 environ(POSIX)
    la      $t0, environ
    st.d    $a2, $t0, 0

    /* C++ 全局对象构造: 正序遍历 .init_array。
     * $r22/$r23 是 callee-saved(见 switch_loongarch64.S), ctor 不会改。 */
    la      $r22, __init_array_start
    la      $r23, __init_array_end
.Linit_loop:
    bge     $r22, $r23, .Linit_done
    ld.d    $t2, $r22, 0
    addi.d  $r22, $r22, 8
    jirl    $r1, $t2, 0
    b       .Linit_loop
.Linit_done:

    bl      main

    /* exit(code): 跑 C++ 析构 + 冲刷标准流缓冲后 syscall 退出 */
    bl      exit
.Lhang:
    b .Lhang

    .section .data
    .global environ
    .type environ, @object
environ:
    .dword 0
    .size environ, 8
