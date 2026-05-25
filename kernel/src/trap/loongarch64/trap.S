    .section .text.trap, "ax"
    .align 2
    .globl __alltraps
    .globl __restore

    /*
     * LoongArch64 TrapContext layout, 8 bytes per slot:
     *   r[0..31]       0..255
     *   prmd           256
     *   era            264
     *   badv           272
     *   estat          280
     * total: 288 bytes
     *
     * This first version saves on the current stack.  It is enough for kernel
     * exception/timer bring-up.  Later user-mode support should switch to a
     * per-task kernel stack before saving this frame.
     */
    .equ TRAP_CONTEXT_SIZE, 288

__alltraps:
    addi.d $r3, $r3, -TRAP_CONTEXT_SIZE

    /* Save r12 before using it as a scratch register. */
    st.d $r12, $r3, 96

    st.d $r0,  $r3, 0
    st.d $r1,  $r3, 8
    st.d $r2,  $r3, 16
    addi.d $r12, $r3, TRAP_CONTEXT_SIZE
    st.d $r12, $r3, 24        /* original sp/r3 */
    st.d $r4,  $r3, 32
    st.d $r5,  $r3, 40
    st.d $r6,  $r3, 48
    st.d $r7,  $r3, 56
    st.d $r8,  $r3, 64
    st.d $r9,  $r3, 72
    st.d $r10, $r3, 80
    st.d $r11, $r3, 88
    st.d $r13, $r3, 104
    st.d $r14, $r3, 112
    st.d $r15, $r3, 120
    st.d $r16, $r3, 128
    st.d $r17, $r3, 136
    st.d $r18, $r3, 144
    st.d $r19, $r3, 152
    st.d $r20, $r3, 160
    st.d $r21, $r3, 168
    st.d $r22, $r3, 176
    st.d $r23, $r3, 184
    st.d $r24, $r3, 192
    st.d $r25, $r3, 200
    st.d $r26, $r3, 208
    st.d $r27, $r3, 216
    st.d $r28, $r3, 224
    st.d $r29, $r3, 232
    st.d $r30, $r3, 240
    st.d $r31, $r3, 248

    csrrd $r12, 0x1           /* PRMD */
    st.d  $r12, $r3, 256
    csrrd $r12, 0x6           /* ERA */
    st.d  $r12, $r3, 264
    csrrd $r12, 0x7           /* BADV */
    st.d  $r12, $r3, 272
    csrrd $r12, 0x5           /* ESTAT */
    st.d  $r12, $r3, 280

    addi.d $r4, $r3, 0        /* a0 = &mut TrapContext */
    la.global $r12, loongarch_trap_handler
    jirl $r1, $r12, 0

__restore:
    addi.d $r3, $r4, 0        /* handler returns context pointer in a0/r4 */

    ld.d $r12, $r3, 256
    csrwr $r12, 0x1           /* PRMD */
    ld.d $r12, $r3, 264
    csrwr $r12, 0x6           /* ERA */

    ld.d $r1,  $r3, 8
    ld.d $r2,  $r3, 16
    /* r3/sp is restored by addi.d below. */
    ld.d $r4,  $r3, 32
    ld.d $r5,  $r3, 40
    ld.d $r6,  $r3, 48
    ld.d $r7,  $r3, 56
    ld.d $r8,  $r3, 64
    ld.d $r9,  $r3, 72
    ld.d $r10, $r3, 80
    ld.d $r11, $r3, 88
    ld.d $r13, $r3, 104
    ld.d $r14, $r3, 112
    ld.d $r15, $r3, 120
    ld.d $r16, $r3, 128
    ld.d $r17, $r3, 136
    ld.d $r18, $r3, 144
    ld.d $r19, $r3, 152
    ld.d $r20, $r3, 160
    ld.d $r21, $r3, 168
    ld.d $r22, $r3, 176
    ld.d $r23, $r3, 184
    ld.d $r24, $r3, 192
    ld.d $r25, $r3, 200
    ld.d $r26, $r3, 208
    ld.d $r27, $r3, 216
    ld.d $r28, $r3, 224
    ld.d $r29, $r3, 232
    ld.d $r30, $r3, 240
    ld.d $r31, $r3, 248
    ld.d $r12, $r3, 96

    addi.d $r3, $r3, TRAP_CONTEXT_SIZE
    ertn
