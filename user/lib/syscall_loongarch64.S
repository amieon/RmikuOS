    .section .text
    .global syscall3
    .type syscall3, @function

syscall3:
    /*
     * C ABI:
     *   a0/r4 = id
     *   a1/r5 = arg0
     *   a2/r6 = arg1
     *   a3/r7 = arg2
     *
     * Kernel syscall ABI:
     *   r11 = id
     *   r4  = arg0
     *   r5  = arg1
     *   r6  = arg2
     */
    or      $r11, $r4, $r0
    or      $r4,  $r5, $r0
    or      $r5,  $r6, $r0
    or      $r6,  $r7, $r0
    syscall 0
    jirl    $zero, $r1, 0



    .section .text
    .global syscall6
    .type syscall6, @function

syscall6:
    /*
     * C ABI:
     *   syscall6(id, arg0, arg1, arg2, arg3, arg4, arg5)
     *
     * On entry:
     *   a0/r4  = id
     *   a1/r5  = arg0
     *   a2/r6  = arg1
     *   a3/r7  = arg2
     *   a4/r8  = arg3
     *   a5/r9  = arg4
     *   a6/r10 = arg5
     *
     * Kernel syscall ABI:
     *   r11 = id
     *   r4  = arg0
     *   r5  = arg1
     *   r6  = arg2
     *   r7  = arg3
     *   r8  = arg4
     *   r9  = arg5
     */
    or      $r11, $r4,  $r0
    or      $r4,  $r5,  $r0
    or      $r5,  $r6,  $r0
    or      $r6,  $r7,  $r0
    or      $r7,  $r8,  $r0
    or      $r8,  $r9,  $r0
    or      $r9,  $r10, $r0
    syscall 0
    jirl    $zero, $r1, 0