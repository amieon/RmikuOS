OUTPUT_ARCH(riscv)
ENTRY(_start)

KERNEL_PHYS_BASE = 0x80200000;
KERNEL_OFFSET    = 0xffffffc000000000;

SECTIONS
{
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

    _kernel_load_start = .;

    . += KERNEL_OFFSET;

    _kernel_start = .;

    .text : AT(ADDR(.text) - KERNEL_OFFSET) {
        _stext = .;
        *(.text)
        *(.text.*)
        _etext = .;
    }

    . = ALIGN(4K);

    .rodata : AT(ADDR(.rodata) - KERNEL_OFFSET) {
        _srodata = .;
        *(.rodata)
        *(.rodata.*)
        _erodata = .;
    }

    . = ALIGN(4K);

    .data : AT(ADDR(.data) - KERNEL_OFFSET) {
        _sdata = .;
        *(.data)
        *(.data.*)

        /*
         * 存储 _start 的物理地址，供 SBI HSM hart_start 使用。
         *
         * 注意：
         *   _start 位于 .text.boot，VMA 本来就是物理地址 0x80200000 附近。
         *   boot_entry_addr 本身位于 high-half .data，
         *   所以 Rust 在正式内核页表启用后仍然可以安全读取它。
         */
        . = ALIGN(8);
        boot_entry_addr = .;
        QUAD(_start);

        *(.data.boot)
        *(.data.boot.*)
        _edata = .;
    }

    . = ALIGN(4K);

    .bss : AT(ADDR(.bss) - KERNEL_OFFSET) {
        _sbss = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        _ebss = .;
    }

    . = ALIGN(4K);

    _kernel_end = .;

    _kernel_start_phys = _kernel_start - KERNEL_OFFSET;
    _kernel_end_phys   = _kernel_end   - KERNEL_OFFSET;
    _rust_main_phys    = rust_main     - KERNEL_OFFSET;

    /DISCARD/ : {
        *(.eh_frame)
        *(.debug_frame)
    }
}