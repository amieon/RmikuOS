    .section .text.trap, "ax"
    .align 2

    .globl __alltraps
    .globl __restore
    .globl __restore_user

    /*
     * RISC-V TrapContext layout, 8 bytes per slot:
     *
     *   x[0..31]       0..255
     *   sstatus        256
     *   sepc           264
     *   stval          272
     *   scause         280
     *
     * total: 288 bytes
     */
    .equ TRAP_CONTEXT_SIZE, 288

    /*
     * sstatus.SPP:
     *   0 => return to user mode
     *   1 => return to supervisor mode
     */
    .equ SSTATUS_SPP, 0x100


/*
 * Trap entry.
 *
 * Kernel convention:
 *   sscratch = 0
 *   sp       = kernel sp
 *
 * User convention:
 *   sscratch = kernel_stack_top
 *   sp       = user sp
 *
 * On trap entry:
 *   csrrw sp, sscratch, sp
 *
 * If trap came from user:
 *   sp       = kernel_stack_top
 *   sscratch = user_sp
 *
 * If trap came from kernel:
 *   sp       = 0
 *   sscratch = original kernel sp
 */
__alltraps:
    csrrw   sp, sscratch, sp

    /*
     * sp != 0 means trap came from user mode.
     */
    bnez    sp, .Ltrap_from_user

    /*
     * Trap came from kernel mode.
     * Recover kernel sp and reset sscratch to 0.
     */
    csrrw   sp, sscratch, sp
    j       .Ltrap_from_kernel


/*
 * Trap from kernel mode.
 *
 * We are already on kernel stack.
 */
.Ltrap_from_kernel:
    addi    sp, sp, -TRAP_CONTEXT_SIZE

    /*
     * Save t0 first before using it as scratch.
     */
    sd      x5,  40(sp)

    sd      x0,   0(sp)
    sd      x1,   8(sp)

    /*
     * Save original kernel sp.
     */
    addi    x5, sp, TRAP_CONTEXT_SIZE
    sd      x5,  16(sp)

    sd      x3,  24(sp)
    sd      x4,  32(sp)
    sd      x6,  48(sp)
    sd      x7,  56(sp)
    sd      x8,  64(sp)
    sd      x9,  72(sp)
    sd      x10, 80(sp)
    sd      x11, 88(sp)
    sd      x12, 96(sp)
    sd      x13, 104(sp)
    sd      x14, 112(sp)
    sd      x15, 120(sp)
    sd      x16, 128(sp)
    sd      x17, 136(sp)
    sd      x18, 144(sp)
    sd      x19, 152(sp)
    sd      x20, 160(sp)
    sd      x21, 168(sp)
    sd      x22, 176(sp)
    sd      x23, 184(sp)
    sd      x24, 192(sp)
    sd      x25, 200(sp)
    sd      x26, 208(sp)
    sd      x27, 216(sp)
    sd      x28, 224(sp)
    sd      x29, 232(sp)
    sd      x30, 240(sp)
    sd      x31, 248(sp)

    csrr    x5, sstatus
    sd      x5, 256(sp)
    csrr    x5, sepc
    sd      x5, 264(sp)
    csrr    x5, stval
    sd      x5, 272(sp)
    csrr    x5, scause
    sd      x5, 280(sp)

    /*
     * Kernel trap handling should not use user-stack handoff.
     */
    csrw    sscratch, zero

    mv      a0, sp
    call    riscv_trap_handler

    j       __restore


/*
 * Trap from user mode.
 *
 * On entry after csrrw:
 *   sp       = kernel_stack_top
 *   sscratch = user_sp
 */
.Ltrap_from_user:
    addi    sp, sp, -TRAP_CONTEXT_SIZE

    /*
     * Save t0 before using it as scratch.
     */
    sd      x5,  40(sp)

    sd      x0,   0(sp)
    sd      x1,   8(sp)

    /*
     * Save user sp from sscratch into x[2] slot.
     */
    csrr    x5, sscratch
    sd      x5,  16(sp)

    sd      x3,  24(sp)
    sd      x4,  32(sp)
    sd      x6,  48(sp)
    sd      x7,  56(sp)
    sd      x8,  64(sp)
    sd      x9,  72(sp)
    sd      x10, 80(sp)
    sd      x11, 88(sp)
    sd      x12, 96(sp)
    sd      x13, 104(sp)
    sd      x14, 112(sp)
    sd      x15, 120(sp)
    sd      x16, 128(sp)
    sd      x17, 136(sp)
    sd      x18, 144(sp)
    sd      x19, 152(sp)
    sd      x20, 160(sp)
    sd      x21, 168(sp)
    sd      x22, 176(sp)
    sd      x23, 184(sp)
    sd      x24, 192(sp)
    sd      x25, 200(sp)
    sd      x26, 208(sp)
    sd      x27, 216(sp)
    sd      x28, 224(sp)
    sd      x29, 232(sp)
    sd      x30, 240(sp)
    sd      x31, 248(sp)

    csrr    x5, sstatus
    sd      x5, 256(sp)
    csrr    x5, sepc
    sd      x5, 264(sp)
    csrr    x5, stval
    sd      x5, 272(sp)
    csrr    x5, scause
    sd      x5, 280(sp)

    /*
     * Now we are in kernel handler.
     * Keep sscratch unchanged? No.
     *
     * During Rust trap handler, we are running on kernel stack.
     * Restore path will set sscratch again before returning to user.
     */
    csrw    sscratch, zero

    mv      a0, sp
    call    riscv_trap_handler

    j       __restore


/*
 * Restore TrapContext.
 *
 * a0 = &mut TrapContext
 *
 * If saved sstatus.SPP == 0:
 *   return to user
 *   set sscratch = kernel_stack_top
 *
 * Else:
 *   return to kernel
 *   set sscratch = 0
 */
__restore_user:
__restore:
    mv      sp, a0

    /*
     * Load saved sstatus and sepc.
     */
    ld      x5, 256(sp)
    ld      x6, 264(sp)

    csrw    sstatus, x5
    csrw    sepc, x6

    /*
     * Decide return target from saved sstatus.SPP.
     */
    andi    x6, x5, SSTATUS_SPP
    beqz    x6, .Lrestore_user
    j       .Lrestore_kernel


/*
 * Restore to kernel mode.
 */
.Lrestore_kernel:
    csrw    sscratch, zero

    ld      x1,   8(sp)
    /*
     * x2/sp restored last.
     */
    ld      x3,  24(sp)
    ld      x4,  32(sp)
    ld      x5,  40(sp)
    ld      x6,  48(sp)
    ld      x7,  56(sp)
    ld      x8,  64(sp)
    ld      x9,  72(sp)
    ld      x10, 80(sp)
    ld      x11, 88(sp)
    ld      x12, 96(sp)
    ld      x13, 104(sp)
    ld      x14, 112(sp)
    ld      x15, 120(sp)
    ld      x16, 128(sp)
    ld      x17, 136(sp)
    ld      x18, 144(sp)
    ld      x19, 152(sp)
    ld      x20, 160(sp)
    ld      x21, 168(sp)
    ld      x22, 176(sp)
    ld      x23, 184(sp)
    ld      x24, 192(sp)
    ld      x25, 200(sp)
    ld      x26, 208(sp)
    ld      x27, 216(sp)
    ld      x28, 224(sp)
    ld      x29, 232(sp)
    ld      x30, 240(sp)
    ld      x31, 248(sp)

    ld      sp,  16(sp)
    sret


/*
 * Restore to user mode.
 */
.Lrestore_user:
    /*
     * TrapContext is placed at:
     *   kernel_stack_top - TRAP_CONTEXT_SIZE
     *
     * So:
     *   kernel_stack_top = cx + TRAP_CONTEXT_SIZE
     *
     * sscratch will be used by __alltraps to switch from user sp
     * to kernel stack on the next user trap.
     */
    addi    x5, sp, TRAP_CONTEXT_SIZE
    csrw    sscratch, x5

    ld      x1,   8(sp)
    /*
     * x2/sp restored last.
     */
    ld      x3,  24(sp)
    ld      x4,  32(sp)
    ld      x5,  40(sp)
    ld      x6,  48(sp)
    ld      x7,  56(sp)
    ld      x8,  64(sp)
    ld      x9,  72(sp)
    ld      x10, 80(sp)
    ld      x11, 88(sp)
    ld      x12, 96(sp)
    ld      x13, 104(sp)
    ld      x14, 112(sp)
    ld      x15, 120(sp)
    ld      x16, 128(sp)
    ld      x17, 136(sp)
    ld      x18, 144(sp)
    ld      x19, 152(sp)
    ld      x20, 160(sp)
    ld      x21, 168(sp)
    ld      x22, 176(sp)
    ld      x23, 184(sp)
    ld      x24, 192(sp)
    ld      x25, 200(sp)
    ld      x26, 208(sp)
    ld      x27, 216(sp)
    ld      x28, 224(sp)
    ld      x29, 232(sp)
    ld      x30, 240(sp)
    ld      x31, 248(sp)

    /*
     * Restore user sp last.
     */
    ld      sp,  16(sp)

    sret