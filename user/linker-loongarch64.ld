OUTPUT_ARCH(loongarch64)
ENTRY(_start)

BASE_ADDRESS = 0x10000;

SECTIONS
{
    . = BASE_ADDRESS;

    .text : {
        KEEP(*(.text.entry))
        KEEP(*(.text.entry.*))

        /* LoongArch 的 bl(R_LARCH_B26)要求目标 4 字节对齐:
         * 若 .text.entry 段(常含数据)大小非 4 倍数, 后续 .text 段起始
         * 地址会错位, 导致跨段 bl 的偏移未对齐而链接失败。 */
        . = ALIGN(4);

        *(.text)
        *(.text.*)

        . = ALIGN(8);

        *(.rodata)
        *(.rodata.*)
        *(.srodata)
        *(.srodata.*)
    }

    . = ALIGN(0x1000);

    .data : {
        *(.data)
        *(.data.*)
        *(.sdata)
        *(.sdata.*)

        . = ALIGN(8);

        *(.got)
        *(.got.*)
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
        *(.plt)
        *(.plt.*)
        *(.dynamic)
        *(.dynsym)
        *(.dynstr)
        *(.gnu.hash)
    }
}
