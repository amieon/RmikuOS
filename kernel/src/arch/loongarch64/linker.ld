/* src/arch/loongarch64/linker.ld */
OUTPUT_ARCH(loongarch64)
ENTRY(_entry)

SECTIONS {
    . = 0x90000000;            /* DMW1 映射起始 */

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