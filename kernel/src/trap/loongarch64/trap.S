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
     *
     * total: 288 bytes
     */
    .equ TRAP_CONTEXT_SIZE, 288

    /*
     * CSR numbers.
     */
    .equ CSR_PRMD,   0x1
    .equ CSR_ESTAT,  0x5
    .equ CSR_ERA,    0x6
    .equ CSR_BADV,   0x7
    .equ CSR_CRMD, 0x0
    .equ CRMD_IE_CLEAR_MASK, -5     /* clear bit 2 */

    /*
     * Kernel save registers.
     *
     * KSAVE0: kernel_stack_top when running user
     * KSAVE1: temporary save for original r12
     * KSAVE2: temporary save for original r13
     */
    .equ CSR_KSAVE0, 0x30
    .equ CSR_KSAVE1, 0x31
    .equ CSR_KSAVE2, 0x32

    .equ PRMD_PPLV_MASK, 0x3
    .equ PLV_USER, 0x3





    .equ UART_BASE_HIGH, 0x900000001fe001e0

    .macro PUTCH_HIGH ch
        li.d    $r20, UART_BASE_HIGH
1:
        ld.bu   $r21, $r20, 5
        andi    $r21, $r21, 0x20
        beqz    $r21, 1b
        li.w    $r21, \ch
        st.b    $r21, $r20, 0
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
 *
 * Important:
 *   r12/r13 are normal registers and must be saved before being used
 *   as scratch registers.
 */
__alltraps:
    PUTCH_HIGH 'T'
    /*
     * Save original r12/r13 immediately.
     *
     * csrwr writes the GPR value into CSR and returns old CSR value
     * into the same GPR, so r12/r13 are clobbered after these two
     * instructions. That is OK because their original values are now
     * in KSAVE1/KSAVE2.
     */
    csrwr   $r12, CSR_KSAVE1
    csrwr   $r13, CSR_KSAVE2

    /*
     * PRMD.PPLV records previous privilege level.
     * If PPLV == 3, trap came from user mode.
     */
    csrrd   $r12, CSR_PRMD
    andi    $r12, $r12, PRMD_PPLV_MASK
    addi.d  $r13, $zero, PLV_USER
    beq     $r12, $r13, .Ltrap_from_user


/*
 * Trap from kernel mode.
 *
 * We are already on a kernel stack.
 */
.Ltrap_from_kernel:

    addi.d  $r3, $r3, -TRAP_CONTEXT_SIZE

    st.d    $r0,  $r3, 0
    st.d    $r1,  $r3, 8
    st.d    $r2,  $r3, 16

    /*
     * Save original kernel sp/r3.
     */
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

    /*
     * Restore original r12/r13 from KSAVE1/KSAVE2 into TrapContext.
     */
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

    /*
     * While handling in kernel, no user kernel-stack handoff is active.
     */
    csrwr   $zero, CSR_KSAVE0

PUTCH_HIGH 'H'

    addi.d  $r4, $r3, 0
    la.global $r12, loongarch_trap_handler
    jirl    $r1, $r12, 0

    b       __restore


/*
 * Trap from user mode.
 *
 * On entry:
 *   r3      = user_sp
 *   KSAVE0  = kernel_stack_top
 *   KSAVE1  = original user r12
 *   KSAVE2  = original user r13
 */
.Ltrap_from_user:
PUTCH_HIGH 'u'
    /*
     * Save user sp temporarily in r13.
     * Original user r13 is already in KSAVE2.
     */
    addi.d  $r13, $r3, 0

    /*
     * Switch to kernel stack:
     *   r12 = kernel_stack_top
     *   r3  = kernel_stack_top - TrapContext size
     */
    csrrd   $r12, CSR_KSAVE0
    addi.d  $r3, $r12, -TRAP_CONTEXT_SIZE

PUTCH_HIGH 's'

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

    /*
     * Restore original user r12/r13 from KSAVE1/KSAVE2 into TrapContext.
     */
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

    /*
     * Now we are in kernel handler.
     */
    csrwr   $zero, CSR_KSAVE0

    PUTCH_HIGH 'H'

    addi.d  $r4, $r3, 0
    la.global $r12, loongarch_trap_handler
    jirl    $r1, $r12, 0

    b       __restore


/*
 * Restore TrapContext.
 *
 * r4 = &mut TrapContext
 *
 * If PRMD.PPLV == 3:
 *   return to user
 *   set KSAVE0 = kernel_stack_top
 *
 * Else:
 *   return to kernel
 *   set KSAVE0 = 0
 */
__restore_user:
__restore:
    addi.d  $r3, $r4, 0

    /*
     * Load new PRMD and new ERA.
     *
     * Important:
     *   csrwr clobbers its source register with old CSR value.
     *   Therefore decide restore target before csrwr.
     */
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
    /* r3/sp restored by addi.d below */
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
    /*
     * Disable current kernel interrupt before setting KSAVE0.
     *
     * Otherwise a timer interrupt between setting KSAVE0 and ertn may
     * enter kernel trap path and clear KSAVE0.
     */
    csrrd   $r12, CSR_CRMD
    li.d    $r13, CRMD_IE_CLEAR_MASK
    and     $r12, $r12, $r13
    csrwr   $r12, CSR_CRMD

    /*
     * TrapContext is placed at:
     *   kernel_stack_top - TRAP_CONTEXT_SIZE
     *
     * So:
     *   kernel_stack_top = cx + TRAP_CONTEXT_SIZE
     */
    addi.d  $r12, $r3, TRAP_CONTEXT_SIZE
    csrwr   $r12, CSR_KSAVE0


    ld.d    $r1,  $r3, 8
    ld.d    $r2,  $r3, 16
    /* r3/sp restored last */
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

    /*
     * Restore user sp last.
     */
    ld.d    $r3,  $r3, 24
    PUTCH_HIGH 'U'

    ertn