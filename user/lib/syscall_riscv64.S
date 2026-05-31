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