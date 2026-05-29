OUTPUT_ARCH(riscv)
ENTRY(_start)

BASE_ADDRESS = 0x10000;

SECTIONS
{
    . = BASE_ADDRESS;

    .text : {
        *(.text.entry)
        *(.text.entry.*)
        *(.text)
        *(.text.*)

        . = ALIGN(8);

        *(.rodata)
        *(.rodata.*)
        *(.srodata)
        *(.srodata.*)

        . = ALIGN(8);

        *(.data)
        *(.data.*)
        *(.sdata)
        *(.sdata.*)
    }

    . = ALIGN(8);

    .bss : {
        *(.bss)
        *(.bss.*)
        *(.sbss)
        *(.sbss.*)
        *(COMMON)
    }

    /DISCARD/ : {
        *(.eh_frame)
        *(.debug_frame)
        *(.note.*)
        *(.comment)
    }
}