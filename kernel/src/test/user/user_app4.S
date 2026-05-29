    .section .text
    .global _start
    .type _start, @function

_start:
    /*
     * write(1, msg0, 20)
     */
    addi.d   $r4,  $r0, 1
    la.local $r5, msg0
    addi.d   $r6,  $r0, 20
    addi.d   $r11, $r0, 2
    syscall 0

    /*
     * busy loop
     */
    li.d     $r12, 200000
1:
    addi.d   $r12, $r12, -1
    bnez     $r12, 1b

    /*
     * write(1, msg1, 19)
     */
    addi.d   $r4,  $r0, 1
    la.local $r5, msg1
    addi.d   $r6,  $r0, 19
    addi.d   $r11, $r0, 2
    syscall 0

    /*
     * exit(0)
     */
    addi.d   $r4,  $r0, 0
    addi.d   $r11, $r0, 0
    syscall 0

2:
    b 2b

msg0:
    .ascii "[user0] busy start\n"

msg1:
    .ascii "[user0] busy end\n"


# loongarch64-unknown-linux-gnu-gcc -nostdlib -static -Ttext=0x10000 user_app4.S -o user_app4.elf
# loongarch64-unknown-linux-gnu-gcc -nostdlib -static -Ttext=0x10000 user_app5.S -o user_app5.elf

# loongarch64-unknown-linux-gnu-objcopy -O binary -j .text user_app4.elf user_app4.bin
# loongarch64-unknown-linux-gnu-objcopy -O binary -j .text user_app5.elf user_app5.bin

# hexdump -v -e '12/1 "0x%02x, " "\n"' user_app4.bin
# echo
# hexdump -v -e '12/1 "0x%02x, " "\n"' user_app5.bin