.section .text.trap, "ax"
    .align 2

    .globl __alltraps
    .globl __restore
    .globl __restore_user

    /*
     * LoongArch64 TrapContext layout, 8 bytes per slot:
     *
     *   r[0..31]       0..255
     *   prmd           256
     *   era            264
     *   badv           272
     *   estat          280
     *   f[0..31]       288..543   (浮点 f0-f31,每个 8 字节)
     *   fcsr           544        (浮点控制状态寄存器 fcsr0)
     *
     * total: 552 bytes
     *
     * 浮点上下文:每次 trap 全保存 f0-f31 + fcsr0,
     * 保证多个浮点用户任务并发时各自浮点状态互不覆盖。
     */
    .equ TRAP_CONTEXT_SIZE, 552

    /*
     * CSR numbers.
     */
    .equ CSR_CRMD,   0x0
    .equ CSR_PRMD,   0x1
    .equ CSR_ESTAT,  0x5
    .equ CSR_ERA,    0x6
    .equ CSR_BADV,   0x7

    /*
     * CRMD bits.
     */
    .equ CRMD_IE_CLEAR_MASK, -5     /* clear bit 2 */

    /*
     * Kernel save registers.
     */
    .equ CSR_KSAVE0, 0x30
    .equ CSR_KSAVE1, 0x31
    .equ CSR_KSAVE2, 0x32

    .equ PRMD_PPLV_MASK, 0x3
    .equ PLV_USER, 0x3

    .equ UART_PADDR, 0x1fe001e0

    .macro PUTCH_HIGH_SAFE ch
        li.d    $r20, -0x800000000000
        li.d    $r21, UART_PADDR
        add.d   $r20, $r20, $r21
    1:
        ld.bu   $r21, $r20, 5
        andi    $r21, $r21, 0x20
        beqz    $r21, 1b
        li.w    $r21, \ch
        st.b    $r21, $r20, 0
    .endm


/*
 * 保存浮点寄存器宏:f0-f31 + fcsr0 存进 TrapContext。
 * 约定:r3 指向 TrapContext 起始,r12 可用作临时(此时 r12 已存)。
 * 必须在整数寄存器(尤其 r12)已保存之后调用。
 */
.macro SAVE_FP_REGS
    fst.d   $f0,  $r3, 288
    fst.d   $f1,  $r3, 296
    fst.d   $f2,  $r3, 304
    fst.d   $f3,  $r3, 312
    fst.d   $f4,  $r3, 320
    fst.d   $f5,  $r3, 328
    fst.d   $f6,  $r3, 336
    fst.d   $f7,  $r3, 344
    fst.d   $f8,  $r3, 352
    fst.d   $f9,  $r3, 360
    fst.d   $f10, $r3, 368
    fst.d   $f11, $r3, 376
    fst.d   $f12, $r3, 384
    fst.d   $f13, $r3, 392
    fst.d   $f14, $r3, 400
    fst.d   $f15, $r3, 408
    fst.d   $f16, $r3, 416
    fst.d   $f17, $r3, 424
    fst.d   $f18, $r3, 432
    fst.d   $f19, $r3, 440
    fst.d   $f20, $r3, 448
    fst.d   $f21, $r3, 456
    fst.d   $f22, $r3, 464
    fst.d   $f23, $r3, 472
    fst.d   $f24, $r3, 480
    fst.d   $f25, $r3, 488
    fst.d   $f26, $r3, 496
    fst.d   $f27, $r3, 504
    fst.d   $f28, $r3, 512
    fst.d   $f29, $r3, 520
    fst.d   $f30, $r3, 528
    fst.d   $f31, $r3, 536
    movfcsr2gr $r12, $fcsr0
    st.d    $r12, $r3, 544
.endm

/*
 * 恢复浮点寄存器宏:从 TrapContext 读回 f0-f31 + fcsr0。
 * 约定:r3 指向 TrapContext 起始,r12 可用作临时(整数尚未恢复)。
 */
.macro RESTORE_FP_REGS
    ld.d    $r12, $r3, 544
    movgr2fcsr $fcsr0, $r12
    fld.d   $f0,  $r3, 288
    fld.d   $f1,  $r3, 296
    fld.d   $f2,  $r3, 304
    fld.d   $f3,  $r3, 312
    fld.d   $f4,  $r3, 320
    fld.d   $f5,  $r3, 328
    fld.d   $f6,  $r3, 336
    fld.d   $f7,  $r3, 344
    fld.d   $f8,  $r3, 352
    fld.d   $f9,  $r3, 360
    fld.d   $f10, $r3, 368
    fld.d   $f11, $r3, 376
    fld.d   $f12, $r3, 384
    fld.d   $f13, $r3, 392
    fld.d   $f14, $r3, 400
    fld.d   $f15, $r3, 408
    fld.d   $f16, $r3, 416
    fld.d   $f17, $r3, 424
    fld.d   $f18, $r3, 432
    fld.d   $f19, $r3, 440
    fld.d   $f20, $r3, 448
    fld.d   $f21, $r3, 456
    fld.d   $f22, $r3, 464
    fld.d   $f23, $r3, 472
    fld.d   $f24, $r3, 480
    fld.d   $f25, $r3, 488
    fld.d   $f26, $r3, 496
    fld.d   $f27, $r3, 504
    fld.d   $f28, $r3, 512
    fld.d   $f29, $r3, 520
    fld.d   $f30, $r3, 528
    fld.d   $f31, $r3, 536
.endm


/*
 * Trap entry.
 *
 * Kernel mode convention:
 *   KSAVE0 = 0
 *   r3/sp  = kernel sp
 *
 * User mode convention:
 *   KSAVE0 = kernel_stack_top
 *   r3/sp  = user sp
 */
__alltraps:

    csrwr   $r12, CSR_KSAVE1
    csrwr   $r13, CSR_KSAVE2

    csrrd   $r12, CSR_PRMD
    andi    $r12, $r12, PRMD_PPLV_MASK
    addi.d  $r13, $zero, PLV_USER
    beq     $r12, $r13, .Ltrap_from_user


/*
 * Trap from kernel mode.
 */
.Ltrap_from_kernel:
    addi.d  $r3, $r3, -TRAP_CONTEXT_SIZE

    st.d    $r0,  $r3, 0
    st.d    $r1,  $r3, 8
    st.d    $r2,  $r3, 16

    addi.d  $r12, $r3, TRAP_CONTEXT_SIZE
    st.d    $r12, $r3, 24

    st.d    $r4,  $r3, 32
    st.d    $r5,  $r3, 40
    st.d    $r6,  $r3, 48
    st.d    $r7,  $r3, 56
    st.d    $r8,  $r3, 64
    st.d    $r9,  $r3, 72
    st.d    $r10, $r3, 80
    st.d    $r11, $r3, 88

    csrrd   $r12, CSR_KSAVE1
    st.d    $r12, $r3, 96

    csrrd   $r12, CSR_KSAVE2
    st.d    $r12, $r3, 104

    st.d    $r14, $r3, 112
    st.d    $r15, $r3, 120
    st.d    $r16, $r3, 128
    st.d    $r17, $r3, 136
    st.d    $r18, $r3, 144
    st.d    $r19, $r3, 152
    st.d    $r20, $r3, 160
    st.d    $r21, $r3, 168
    st.d    $r22, $r3, 176
    st.d    $r23, $r3, 184
    st.d    $r24, $r3, 192
    st.d    $r25, $r3, 200
    st.d    $r26, $r3, 208
    st.d    $r27, $r3, 216
    st.d    $r28, $r3, 224
    st.d    $r29, $r3, 232
    st.d    $r30, $r3, 240
    st.d    $r31, $r3, 248

    csrrd   $r12, CSR_PRMD
    st.d    $r12, $r3, 256
    csrrd   $r12, CSR_ERA
    st.d    $r12, $r3, 264
    csrrd   $r12, CSR_BADV
    st.d    $r12, $r3, 272
    csrrd   $r12, CSR_ESTAT
    st.d    $r12, $r3, 280

    /* 保存浮点(r12 已存,可用作临时) */
    SAVE_FP_REGS

    csrwr   $zero, CSR_KSAVE0

    addi.d  $r4, $r3, 0
    la.global $r12, loongarch_trap_handler
    jirl    $r1, $r12, 0

    b       __restore


/*
 * Trap from user mode.
 */
.Ltrap_from_user:
    addi.d  $r13, $r3, 0

    csrrd   $r12, CSR_KSAVE0
    addi.d  $r3, $r12, -TRAP_CONTEXT_SIZE

    st.d    $r0,  $r3, 0
    st.d    $r1,  $r3, 8
    st.d    $r2,  $r3, 16
    st.d    $r13, $r3, 24        /* user sp */

    st.d    $r4,  $r3, 32
    st.d    $r5,  $r3, 40
    st.d    $r6,  $r3, 48
    st.d    $r7,  $r3, 56
    st.d    $r8,  $r3, 64
    st.d    $r9,  $r3, 72
    st.d    $r10, $r3, 80
    st.d    $r11, $r3, 88

    csrrd   $r12, CSR_KSAVE1
    st.d    $r12, $r3, 96

    csrrd   $r12, CSR_KSAVE2
    st.d    $r12, $r3, 104

    st.d    $r14, $r3, 112
    st.d    $r15, $r3, 120
    st.d    $r16, $r3, 128
    st.d    $r17, $r3, 136
    st.d    $r18, $r3, 144
    st.d    $r19, $r3, 152
    st.d    $r20, $r3, 160
    st.d    $r21, $r3, 168
    st.d    $r22, $r3, 176
    st.d    $r23, $r3, 184
    st.d    $r24, $r3, 192
    st.d    $r25, $r3, 200
    st.d    $r26, $r3, 208
    st.d    $r27, $r3, 216
    st.d    $r28, $r3, 224
    st.d    $r29, $r3, 232
    st.d    $r30, $r3, 240
    st.d    $r31, $r3, 248

    csrrd   $r12, CSR_PRMD
    st.d    $r12, $r3, 256
    csrrd   $r12, CSR_ERA
    st.d    $r12, $r3, 264
    csrrd   $r12, CSR_BADV
    st.d    $r12, $r3, 272
    csrrd   $r12, CSR_ESTAT
    st.d    $r12, $r3, 280

    /* 保存浮点 */
    SAVE_FP_REGS

    csrwr   $zero, CSR_KSAVE0

    addi.d  $r4, $r3, 0
    la.global $r12, loongarch_trap_handler
    jirl    $r1, $r12, 0

    b       __restore


/*
 * Restore TrapContext.
 *
 * r4 = &mut TrapContext
 */
__restore_user:
__restore:

    addi.d  $r3, $r4, 0

    /* 先恢复浮点(此时整数寄存器尚未恢复,r12 可自由用) */
    RESTORE_FP_REGS

    ld.d    $r12, $r3, 256      /* new PRMD */
    ld.d    $r13, $r3, 264      /* new ERA */

    andi    $r14, $r12, PRMD_PPLV_MASK
    addi.d  $r15, $zero, PLV_USER

    csrwr   $r12, CSR_PRMD
    csrwr   $r13, CSR_ERA

    beq     $r14, $r15, .Lrestore_user
    b       .Lrestore_kernel


/*
 * Restore to kernel mode.
 */
.Lrestore_kernel:

    csrwr   $zero, CSR_KSAVE0

    ld.d    $r1,  $r3, 8
    ld.d    $r2,  $r3, 16
    ld.d    $r4,  $r3, 32
    ld.d    $r5,  $r3, 40
    ld.d    $r6,  $r3, 48
    ld.d    $r7,  $r3, 56
    ld.d    $r8,  $r3, 64
    ld.d    $r9,  $r3, 72
    ld.d    $r10, $r3, 80
    ld.d    $r11, $r3, 88
    ld.d    $r12, $r3, 96
    ld.d    $r13, $r3, 104
    ld.d    $r14, $r3, 112
    ld.d    $r15, $r3, 120
    ld.d    $r16, $r3, 128
    ld.d    $r17, $r3, 136
    ld.d    $r18, $r3, 144
    ld.d    $r19, $r3, 152
    ld.d    $r20, $r3, 160
    ld.d    $r21, $r3, 168
    ld.d    $r22, $r3, 176
    ld.d    $r23, $r3, 184
    ld.d    $r24, $r3, 192
    ld.d    $r25, $r3, 200
    ld.d    $r26, $r3, 208
    ld.d    $r27, $r3, 216
    ld.d    $r28, $r3, 224
    ld.d    $r29, $r3, 232
    ld.d    $r30, $r3, 240
    ld.d    $r31, $r3, 248

    addi.d  $r3, $r3, TRAP_CONTEXT_SIZE
    ertn


/*
 * Restore to user mode.
 */
.Lrestore_user:

    csrrd   $r12, CSR_CRMD
    li.d    $r13, CRMD_IE_CLEAR_MASK
    and     $r12, $r12, $r13
    csrwr   $r12, CSR_CRMD

    addi.d  $r12, $r3, TRAP_CONTEXT_SIZE
    csrwr   $r12, CSR_KSAVE0

    ld.d    $r1,  $r3, 8
    ld.d    $r2,  $r3, 16
    ld.d    $r4,  $r3, 32
    ld.d    $r5,  $r3, 40
    ld.d    $r6,  $r3, 48
    ld.d    $r7,  $r3, 56
    ld.d    $r8,  $r3, 64
    ld.d    $r9,  $r3, 72
    ld.d    $r10, $r3, 80
    ld.d    $r11, $r3, 88
    ld.d    $r12, $r3, 96
    ld.d    $r13, $r3, 104
    ld.d    $r14, $r3, 112
    ld.d    $r15, $r3, 120
    ld.d    $r16, $r3, 128
    ld.d    $r17, $r3, 136
    ld.d    $r18, $r3, 144
    ld.d    $r19, $r3, 152
    ld.d    $r20, $r3, 160
    ld.d    $r21, $r3, 168
    ld.d    $r22, $r3, 176
    ld.d    $r23, $r3, 184
    ld.d    $r24, $r3, 192
    ld.d    $r25, $r3, 200
    ld.d    $r26, $r3, 208
    ld.d    $r27, $r3, 216
    ld.d    $r28, $r3, 224
    ld.d    $r29, $r3, 232
    ld.d    $r30, $r3, 240
    ld.d    $r31, $r3, 248

    ld.d    $r3,  $r3, 24

    ertn