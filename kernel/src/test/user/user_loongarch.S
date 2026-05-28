    .section .text
    .globl _start

_start:
    addi.d  $r4,  $r0, 0      /* a0 = 0 */
    addi.d  $r5,  $r0, 0      /* a1 = 0 */
    addi.d  $r6,  $r0, 0      /* a2 = 0 */
    addi.d  $r11, $r0, 0      /* a7 = syscall id 0 */
    syscall 0

1:
    b 1b