OUTPUT_ARCH(riscv)
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
        *(.eh_frame)
        *(.gcc_except_table)
        *(.gcc_except_table.*)
    }

    . = ALIGN(0x1000);

    .data : {
        *(.data)
        *(.data.*)
        *(.sdata)
        *(.sdata.*)


        . = ALIGN(8);
        __init_array_start = .;
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = .;

        . = ALIGN(8);
        __fini_array_start = .;
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array))
        __fini_array_end = .;
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
        *(.debug_frame)
        *(.note.*)
        *(.comment)
        *(.riscv.attributes)
    }
}
