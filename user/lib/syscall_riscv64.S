    .section .text
    .global syscall3
    .type syscall3, @function

/*
 * C ABI:
 *   syscall3(id, arg0, arg1, arg2)
 *
 * RISC-V syscall ABI used by kernel:
 *   a7 = id
 *   a0 = arg0
 *   a1 = arg1
 *   a2 = arg2
 *
 * On entry:
 *   a0 = id
 *   a1 = arg0
 *   a2 = arg1
 *   a3 = arg2
 */
syscall3:
    mv      a7, a0
    mv      a0, a1
    mv      a1, a2
    mv      a2, a3
    ecall
    ret


    .section .text
    .global syscall6
    .type syscall6, @function

/*
 * C ABI:
 *   syscall6(id, arg0, arg1, arg2, arg3, arg4, arg5)
 *
 * On entry:
 *   a0 = id
 *   a1 = arg0
 *   a2 = arg1
 *   a3 = arg2
 *   a4 = arg3
 *   a5 = arg4
 *   a6 = arg5
 *
 * Kernel syscall ABI:
 *   a7 = id
 *   a0 = arg0
 *   a1 = arg1
 *   a2 = arg2
 *   a3 = arg3
 *   a4 = arg4
 *   a5 = arg5
 */
syscall6:
    mv      a7, a0
    mv      a0, a1
    mv      a1, a2
    mv      a2, a3
    mv      a3, a4
    mv      a4, a5
    mv      a5, a6
    ecall
    ret