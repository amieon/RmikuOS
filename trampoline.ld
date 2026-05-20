/* trampoline.ld —— 链接到 QEMU 复位向量 0x1c000000 */
OUTPUT_ARCH(loongarch64)
ENTRY(_start)

SECTIONS
{
    . = 0x000000001c000000;
    .text : {
        *(.text)
    }
}