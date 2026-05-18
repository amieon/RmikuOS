/* src/arch/riscv64/linker.ld */
OUTPUT_ARCH(riscv64)
ENTRY(_start)

SECTIONS {
    . = 0x80200000;          

    .text : {
        *(.text.init)       
        *(.text.*)
    }

    .rodata : { *(.rodata.*) }
    .data   : { *(.data.*) }

    .bss : {
        *(.bss.*)
        *(COMMON)
    }

    . = ALIGN(4096);
    .bss.stack (NOLOAD) : {
        *(.bss.stack)
    }
}