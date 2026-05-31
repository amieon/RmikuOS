OUTPUT_ARCH(loongarch64)
ENTRY(_start)

BASE_ADDRESS = 0x10000;

SECTIONS
{
    . = BASE_ADDRESS;

    .text : {
        KEEP(*(.text.entry))
        KEEP(*(.text.entry.*))

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