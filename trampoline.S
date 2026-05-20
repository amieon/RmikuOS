/* trampoline.S */
.section .text
.global _start
_start:
    /* 直接跳转到内核 _entry（0x1000000） */
    lu12i.w     $t0, 0x1000         /* $t0 = 0x1000000 */
    jirl        $zero, $t0, 0

1:  b       1b