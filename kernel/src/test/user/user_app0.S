    .section .text
    .global _start
    .type _start, @function

_start:
    /*
     * syscall yield()
     *
     * LoongArch ABI:
     *   a0 = r4
     *   a1 = r5
     *   a2 = r6
     *   a7 = r11
     *
     * syscall id:
     *   0 = exit(code)
     *   1 = yield()
     */

    addi.d  $r4,  $r0, 0      /* a0 = 0 */
    addi.d  $r5,  $r0, 0      /* a1 = 0 */
    addi.d  $r6,  $r0, 0      /* a2 = 0 */
    addi.d  $r11, $r0, 1      /* a7 = 1, yield */
    syscall 0

    /*
     * syscall exit(0)
     */
    addi.d  $r4,  $r0, 0      /* a0 = exit code 0 */
    addi.d  $r5,  $r0, 0
    addi.d  $r6,  $r0, 0
    addi.d  $r11, $r0, 0      /* a7 = 0, exit */
    syscall 0

1:
    b 1b