/* kernel/src/arch/riscv64/linker.ld */
OUTPUT_ARCH(riscv64)
ENTRY(_start)

BASE_ADDRESS = 0x80200000;

SECTIONS {
    . = BASE_ADDRESS;
    _kernel_start = .;

    .text : ALIGN(4K) {
        _stext = .;
        *(.text.init)
        *(.text .text.*)
        _etext = .;
    }

    .rodata : ALIGN(4K) {
        _srodata = .;
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
        _erodata = .;
    }

    .data : ALIGN(4K) {
        _sdata = .;
        *(.data .data.*)
        *(.sdata .sdata.*)
        _edata = .;
    }

    .bss (NOLOAD) : ALIGN(4K) {
        _sbss = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
        . = ALIGN(4K);
        *(.bss.stack)
        _ebss = .;
    }

    . = ALIGN(4K);
    _kernel_end = .;

    /DISCARD/ : {
        *(.eh_frame)
        *(.comment)
        *(.note*)
    }
}
