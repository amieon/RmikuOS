OUTPUT_ARCH(loongarch64)
ENTRY(_start)
EXTERN(rust_main)

KERNEL_PHYS_BASE = 0x01000000;
KERNEL_OFFSET    = 0x9000000000000000;

PHDRS
{
    boot   PT_LOAD FLAGS(5);  /* R-X */
    text   PT_LOAD FLAGS(5);  /* R-X */
    rodata PT_LOAD FLAGS(4);  /* R-- */
    data   PT_LOAD FLAGS(6);  /* RW- */
}

SECTIONS
{
    . = KERNEL_PHYS_BASE;

    .text.boot : {
        KEEP(*(.text.boot))
        KEEP(*(.text.boot.*))
    } :boot

    . = ALIGN(4K);

    .bss.boot (NOLOAD) : {
        KEEP(*(.bss.boot))
        KEEP(*(.bss.boot.*))
    } :boot

    . = ALIGN(4K);

    _kernel_load_start = .;
    . += KERNEL_OFFSET;

    _kernel_start = .;

    .text : AT(ADDR(.text) - KERNEL_OFFSET) {
        _stext = .;
        KEEP(*(.text))
        KEEP(*(.text.*))
        _etext = .;
    } :text

    . = ALIGN(4K);

    .rodata : AT(ADDR(.rodata) - KERNEL_OFFSET) {
        _srodata = .;
        KEEP(*(.rodata))
        KEEP(*(.rodata.*))
        KEEP(*(.srodata))
        KEEP(*(.srodata.*))
        _erodata = .;
    } :rodata

    . = ALIGN(4K);

    .data : AT(ADDR(.data) - KERNEL_OFFSET) {
        _sdata = .;
        KEEP(*(.data))
        KEEP(*(.data.*))
        KEEP(*(.sdata))
        KEEP(*(.sdata.*))
        _edata = .;
    } :data

    . = ALIGN(4K);

    .bss (NOLOAD) : AT(ADDR(.bss) - KERNEL_OFFSET) {
        _sbss = .;
        KEEP(*(.bss))
        KEEP(*(.bss.*))
        KEEP(*(.sbss))
        KEEP(*(.sbss.*))
        KEEP(*(COMMON))
        _ebss = .;
    } :data

    . = ALIGN(4K);

    _kernel_end = .;

    _kernel_start_phys = _kernel_start - KERNEL_OFFSET;
    _kernel_end_phys   = _kernel_end   - KERNEL_OFFSET;
    _rust_main_phys    = rust_main     - KERNEL_OFFSET;

    _sbss_phys = _sbss - KERNEL_OFFSET;
    _ebss_phys = _ebss - KERNEL_OFFSET;

    /DISCARD/ : {
        *(.eh_frame)
        *(.debug_frame)
        *(.note.gnu.build-id)
        *(.note.*)
        *(.comment)
    }
}