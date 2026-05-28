    .section .text
    .globl __restore_user
    .type __restore_user, @function

/*
 * a0/r4 = *const TrapContext
 *
 * TrapContext layout:
 * r[32]  0..255
 * prmd   256
 * era    264
 * badv   272
 * estat  280
 */
__restore_user:
    /*
     * 设置 ertn 返回状态。
     */
    ld.d    $r12, $r4, 256
    csrwr   $r12, 0x1        /* PRMD */

    ld.d    $r12, $r4, 264
    csrwr   $r12, 0x6        /* ERA */

    /*
     * 恢复通用寄存器。
     * r4/a0 是 TrapContext 指针，最后恢复。
     */
    ld.d    $r1,  $r4,  8
    ld.d    $r2,  $r4, 16
    ld.d    $r3,  $r4, 24

    ld.d    $r5,  $r4, 40
    ld.d    $r6,  $r4, 48
    ld.d    $r7,  $r4, 56
    ld.d    $r8,  $r4, 64
    ld.d    $r9,  $r4, 72
    ld.d    $r10, $r4, 80
    ld.d    $r11, $r4, 88
    ld.d    $r12, $r4, 96
    ld.d    $r13, $r4, 104
    ld.d    $r14, $r4, 112
    ld.d    $r15, $r4, 120
    ld.d    $r16, $r4, 128
    ld.d    $r17, $r4, 136
    ld.d    $r18, $r4, 144
    ld.d    $r19, $r4, 152
    ld.d    $r20, $r4, 160
    ld.d    $r21, $r4, 168
    ld.d    $r22, $r4, 176
    ld.d    $r23, $r4, 184
    ld.d    $r24, $r4, 192
    ld.d    $r25, $r4, 200
    ld.d    $r26, $r4, 208
    ld.d    $r27, $r4, 216
    ld.d    $r28, $r4, 224
    ld.d    $r29, $r4, 232
    ld.d    $r30, $r4, 240
    ld.d    $r31, $r4, 248

    /*
     * 最后恢复 r4/a0。
     */
    ld.d    $r4,  $r4, 32

    ertn