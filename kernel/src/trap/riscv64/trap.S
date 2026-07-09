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
     *   f[0..31]       288..543   (浮点 f0-f31,每个 8 字节)
     *   fcsr           544
     *
     * total: 552 bytes
     *
     * 浮点上下文:每次 trap 全保存 f0-f31 + fcsr,
     * 保证多个浮点用户任务并发时各自浮点状态不被覆盖。
     */
    .equ TRAP_CONTEXT_SIZE, 552

    /*
     * sstatus.SPP:
     *   0 => return to user mode
     *   1 => return to supervisor mode
     */
    .equ SSTATUS_SPP, 0x100
    .equ SSTATUS_FS_DIRTY, 0x6000


/*
 * 保存浮点寄存器宏:把 f0-f31 + fcsr 存进 TrapContext。
 * 约定:sp 指向 TrapContext 起始,x5(t0)可用作临时。
 * 必须在整数寄存器(尤其 x5)已保存之后调用。
 */
.macro SAVE_FP_REGS
    li      x5, SSTATUS_FS_DIRTY
    csrs    sstatus, x5

    fsd  f0,  288(sp)
    fsd  f1,  296(sp)
    fsd  f2,  304(sp)
    fsd  f3,  312(sp)
    fsd  f4,  320(sp)
    fsd  f5,  328(sp)
    fsd  f6,  336(sp)
    fsd  f7,  344(sp)
    fsd  f8,  352(sp)
    fsd  f9,  360(sp)
    fsd  f10, 368(sp)
    fsd  f11, 376(sp)
    fsd  f12, 384(sp)
    fsd  f13, 392(sp)
    fsd  f14, 400(sp)
    fsd  f15, 408(sp)
    fsd  f16, 416(sp)
    fsd  f17, 424(sp)
    fsd  f18, 432(sp)
    fsd  f19, 440(sp)
    fsd  f20, 448(sp)
    fsd  f21, 456(sp)
    fsd  f22, 464(sp)
    fsd  f23, 472(sp)
    fsd  f24, 480(sp)
    fsd  f25, 488(sp)
    fsd  f26, 496(sp)
    fsd  f27, 504(sp)
    fsd  f28, 512(sp)
    fsd  f29, 520(sp)
    fsd  f30, 528(sp)
    fsd  f31, 536(sp)
    frcsr x5
    sd   x5, 544(sp)
.endm

/*
 * 恢复浮点寄存器宏:从 TrapContext 读回 f0-f31 + fcsr。
 * 约定:sp 指向 TrapContext 起始,x5 可用作临时。
 * 必须在恢复整数寄存器(尤其会覆盖 x5)之前调用。
 */
.macro RESTORE_FP_REGS
    li      x5, SSTATUS_FS_DIRTY
    csrs    sstatus, x5

    ld   x5, 544(sp)
    fscsr x5
    fld  f0,  288(sp)
    fld  f1,  296(sp)
    fld  f2,  304(sp)
    fld  f3,  312(sp)
    fld  f4,  320(sp)
    fld  f5,  328(sp)
    fld  f6,  336(sp)
    fld  f7,  344(sp)
    fld  f8,  352(sp)
    fld  f9,  360(sp)
    fld  f10, 368(sp)
    fld  f11, 376(sp)
    fld  f12, 384(sp)
    fld  f13, 392(sp)
    fld  f14, 400(sp)
    fld  f15, 408(sp)
    fld  f16, 416(sp)
    fld  f17, 424(sp)
    fld  f18, 432(sp)
    fld  f19, 440(sp)
    fld  f20, 448(sp)
    fld  f21, 456(sp)
    fld  f22, 464(sp)
    fld  f23, 472(sp)
    fld  f24, 480(sp)
    fld  f25, 488(sp)
    fld  f26, 496(sp)
    fld  f27, 504(sp)
    fld  f28, 512(sp)
    fld  f29, 520(sp)
    fld  f30, 528(sp)
    fld  f31, 536(sp)
.endm


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
 */
.Ltrap_from_kernel:
    addi    sp, sp, -TRAP_CONTEXT_SIZE

    sd      x5,  40(sp)

    sd      x0,   0(sp)
    sd      x1,   8(sp)

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

    /* 保存浮点(x5 已存,可用作临时) */
    SAVE_FP_REGS

    csrw    sscratch, zero

    mv      a0, sp
    call    riscv_trap_handler

    j       __restore


/*
 * Trap from user mode.
 */
.Ltrap_from_user:
    addi    sp, sp, -TRAP_CONTEXT_SIZE

    sd      x5,  40(sp)

    sd      x0,   0(sp)
    sd      x1,   8(sp)

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

    /* 保存浮点 */
    SAVE_FP_REGS

    csrw    sscratch, zero

    mv      a0, sp
    call    riscv_trap_handler

    j       __restore


/*
 * Restore TrapContext.
 *
 * a0 = &mut TrapContext
 */
__restore_user:
__restore:
    mv      sp, a0

    /* 先恢复浮点(此时 x5 还能自由用,整数寄存器尚未恢复) */
    RESTORE_FP_REGS

    ld      x5, 256(sp)
    ld      x6, 264(sp)

    csrw    sstatus, x5
    csrw    sepc, x6

    andi    x6, x5, SSTATUS_SPP
    beqz    x6, .Lrestore_user
    j       .Lrestore_kernel


/*
 * Restore to kernel mode.
 */
.Lrestore_kernel:
    csrw    sscratch, zero

    ld      x1,   8(sp)
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
    addi    x5, sp, TRAP_CONTEXT_SIZE
    csrw    sscratch, x5

    ld      x1,   8(sp)
    ld      x3,  24(sp)
    //ld      x4,  32(sp)
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