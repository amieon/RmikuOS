    .section .text
    .global _start
    .type _start, @function

_start:
    /*
     * syscall yield()
     */
    addi.d  $r4,  $r0, 0      /* a0 = 0 */
    addi.d  $r5,  $r0, 0      /* a1 = 0 */
    addi.d  $r6,  $r0, 0      /* a2 = 0 */
    addi.d  $r11, $r0, 1      /* a7 = 1, yield */
    syscall 0

    /*
     * syscall exit(1)
     */
    addi.d  $r4,  $r0, 1      /* a0 = exit code 1 */
    addi.d  $r5,  $r0, 0
    addi.d  $r6,  $r0, 0
    addi.d  $r11, $r0, 0      /* a7 = 0, exit */
    syscall 0

1:
    b 1b