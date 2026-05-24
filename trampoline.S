/*
 * Minimal LoongArch64 QEMU trampoline.
 *
 * QEMU loads this raw binary as BIOS. The real kernel binary is loaded by
 * run.sh with -device loader at 0x1000000, so the only job here is to jump
 * to that physical address.
 */
    .section .text.boot, "ax"
    .global _start
    .type _start, @function

_start:
    lu12i.w $t0, 0x1000        /* $t0 = 0x1000000 */
    jirl    $zero, $t0, 0      /* pc = 0x1000000, no return */
