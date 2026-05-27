OUTPUT_ARCH(loongarch64)
ENTRY(_start)

KERNEL_PHYS_BASE = 0x01000000;
KERNEL_OFFSET    = 0x9000000000000000;

SECTIONS
{
    /*
     * Low boot area.
     * QEMU loader places the kernel at 0x01000000.
     * Before paging/DMW, CPU can only execute low physical addresses.
     */
    . = KERNEL_PHYS_BASE;

    .text.boot : {
        KEEP(*(.text.boot))
        KEEP(*(.text.boot.*))
    }

    . = ALIGN(4K);

    .bss.boot (NOLOAD) : {
        KEEP(*(.bss.boot))
        KEEP(*(.bss.boot.*))
    }

    . = ALIGN(4K);

    /*
     * High-half kernel.
     * VMA = LMA + KERNEL_OFFSET.
     */
    _kernel_load_start = .;
    . += KERNEL_OFFSET;

    _kernel_start = .;

    .text : AT(ADDR(.text) - KERNEL_OFFSET) {
        _stext = .;
        *(.text .text.*)
        _etext = .;
    }

    . = ALIGN(4K);

    .rodata : AT(ADDR(.rodata) - KERNEL_OFFSET) {
        _srodata = .;
        *(.rodata .rodata.*)
        _erodata = .;
    }

    . = ALIGN(4K);

    .data : AT(ADDR(.data) - KERNEL_OFFSET) {
        _sdata = .;
        *(.data .data.*)
        _edata = .;
    }

    . = ALIGN(4K);

    .bss : AT(ADDR(.bss) - KERNEL_OFFSET) {
        _sbss = .;
        *(.bss .bss.*)
        *(COMMON)
        _ebss = .;
    }

    . = ALIGN(4K);

    _kernel_end = .;

    _rust_main_phys = rust_main - KERNEL_OFFSET;

    /DISCARD/ : {
        *(.eh_frame)
        *(.debug_frame)
    }
}