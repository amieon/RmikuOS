    .section .text.entry
    .global _start
    .type _start, @function

_start:
    # 保存 envp(a2) 到全局 environ(POSIX)
    la      $t0, environ
    st.d    $a2, $t0, 0

    bl      main

    addi.d  $r11, $r0, 0
    syscall 0

1:
    b 1b

    .section .data
    .global environ
    .type environ, @object
environ:
    .dword 0
    .size environ, 8