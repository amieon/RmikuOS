    .section .text
    .global _start
    .type _start, @function

_start:
    /*
     * write(1, msg0, 21)
     */
    addi.d   $r4,  $r0, 1
    la.local $r5, msg0
    addi.d   $r6,  $r0, 21
    addi.d   $r11, $r0, 2
    syscall 0

    /*
     * yield()
     */
    addi.d   $r11, $r0, 1
    syscall 0

    /*
     * write(1, msg1, 20)
     */
    addi.d   $r4,  $r0, 1
    la.local $r5, msg1
    addi.d   $r6,  $r0, 20
    addi.d   $r11, $r0, 2
    syscall 0

    /*
     * exit(1)
     */
    addi.d   $r4,  $r0, 1
    addi.d   $r11, $r0, 0
    syscall 0

1:
    b 1b

msg0:
    .ascii "[user1] before yield\n"

msg1:
    .ascii "[user1] after yield\n"


# loongarch64-unknown-linux-gnu-gcc -nostdlib -static -Ttext=0x10000 user_app2.S -o user_app2.elf
# loongarch64-unknown-linux-gnu-gcc -nostdlib -static -Ttext=0x10000 user_app3.S -o user_app3.elf

# loongarch64-unknown-linux-gnu-objcopy -O binary -j .text user_app2.elf user_app2.bin
# loongarch64-unknown-linux-gnu-objcopy -O binary -j .text user_app3.elf user_app3.bin

# hexdump -v -e '12/1 "0x%02x, " "\n"' user_app2.bin
# echo
# hexdump -v -e '12/1 "0x%02x, " "\n"' user_app3.bin