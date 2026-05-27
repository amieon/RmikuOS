OUTPUT_ARCH(riscv)
ENTRY(_start)

KERNEL_PHYS_BASE = 0x80200000;
KERNEL_OFFSET = 0xffffffc000000000;
KERNEL_VIRT_BASE = KERNEL_PHYS_BASE + KERNEL_OFFSET;

SECTIONS {
    . = KERNEL_PHYS_BASE;

    .text.boot : {
        *(.text.boot)
    }

    .bss.boot : {
        *(.bss.boot)
    }

    . = KERNEL_VIRT_BASE;

    _kernel_start = .;

    .text : AT(ADDR(.text) - KERNEL_OFFSET) {
        _stext = .;
        *(.text .text.*)
        _etext = .;
    }

    .rodata : AT(ADDR(.rodata) - KERNEL_OFFSET) {
        _srodata = .;
        *(.rodata .rodata.*)
        _erodata = .;
    }

    .data : AT(ADDR(.data) - KERNEL_OFFSET) {
        _sdata = .;
        *(.data .data.*)
        _edata = .;
    }

    .bss : AT(ADDR(.bss) - KERNEL_OFFSET) {
        _sbss = .;
        *(.bss .bss.*)
        *(COMMON)
        _ebss = .;
    }

    _kernel_end = .;

    _kernel_start_phys = _kernel_start - KERNEL_OFFSET;
    _kernel_end_phys = _kernel_end - KERNEL_OFFSET;
    _rust_main_phys = rust_main - KERNEL_OFFSET;
}