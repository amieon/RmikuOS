/* kernel/src/arch/loongarch64/linker.ld */
OUTPUT_ARCH(loongarch64)
ENTRY(_entry)

MEMORY {
    RAM (rwx) : ORIGIN = 0x0000000001000000, LENGTH = 128M
}

SECTIONS {
    . = ORIGIN(RAM);
    _kernel_start = .;

    .text : ALIGN(4K) {
        _stext = .;
        *(.text.boot)
        *(.text .text.*)
        _etext = .;
    } > RAM

    .rodata : ALIGN(4K) {
        _srodata = .;
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
        _erodata = .;
    } > RAM

    .data : ALIGN(4K) {
        _sdata = .;
        *(.data .data.*)
        *(.sdata .sdata.*)
        _edata = .;
    } > RAM

    .bss (NOLOAD) : ALIGN(4K) {
        _sbss = .;
        _bss_start = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
        . = ALIGN(4K);
        *(.bss.stack)
        . = ALIGN(4K);
        _bss_end = .;
        _ebss = .;
    } > RAM

    . = ALIGN(4K);
    _kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
