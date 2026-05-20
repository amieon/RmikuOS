OUTPUT_ARCH(loongarch64)
ENTRY(_entry)

MEMORY
{
    RAM (rwx) : ORIGIN = 0x0000000001000000, LENGTH = 128M
}

SECTIONS
{
    . = ORIGIN(RAM);
    _kernel_start = .;

    /* 强制 .text.boot 在最前面，_entry 就在 0x1000000 */
    .text : ALIGN(16) {
        *(.text.boot)
        *(.text*)
        *(.text.*)
    } > RAM

    .rodata : ALIGN(16) {
        *(.rodata*)
        *(.srodata*)
    } > RAM

    .data : ALIGN(16) {
        *(.data*)
        *(.sdata*)
    } > RAM

    . = ALIGN(16);
    .bss (NOLOAD) : ALIGN(16) {
        _bss_start = .;
        *(.bss*)
        *(.sbss*)
        *(COMMON)
        . = ALIGN(12);
        *(.bss.stack)
        . = ALIGN(16);
        _bss_end = .;
    } > RAM

    . = ALIGN(16);
    _kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}