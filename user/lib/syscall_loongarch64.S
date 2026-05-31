    .section .text
    .global syscall3
    .type syscall3, @function

/*
 * C ABI:
 *   syscall3(id, arg0, arg1, arg2)
 *
 * LoongArch C ABI on entry:
 *   r4 = id
 *   r5 = arg0
 *   r6 = arg1
 *   r7 = arg2
 *
 * Kernel syscall ABI:
 *   r11 = id
 *   r4  = arg0
 *   r5  = arg1
 *   r6  = arg2
 */
syscall3:
    or      $r11, $r4, $r0
    or      $r4,  $r5, $r0
    or      $r5,  $r6, $r0
    or      $r6,  $r7, $r0
    syscall 0
    jirl    $zero, $r1, 0