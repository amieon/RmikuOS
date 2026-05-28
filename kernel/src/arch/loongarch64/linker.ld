OUTPUT_ARCH(loongarch64)
ENTRY(_start)

EXTERN(rust_main)
EXTERN(__tlb_refill)

KERNEL_PHYS_BASE = 0x01000000;
KERNEL_OFFSET    = 0xffff800000000000;

/*
 * Must match boot.S.
 */
BOOT_MAP_SIZE  = 0x10000000;
BOOT_PT_PAGES  = BOOT_MAP_SIZE / 0x200000;
STACK_SIZE     = 65536;
MAX_HARTS      = 8;

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

    _boot_start = .;

    .text.boot : {
        KEEP(*(.text.boot))
        KEEP(*(.text.boot.*))
    } :boot

    . = ALIGN(4K);

    /*
     * Linker-owned boot scratch.
     *
     * Do not rely on .space in boot.S for this.  These symbols are used
     * directly by boot.S with la.local.
     */
    _boot_scratch_start = .;

    boot_pg_start = .;

    boot_pgd = .;
    . += 4096;

    boot_dir2 = .;
    . += 4096;

    boot_dir1 = .;
    . += 4096;

    boot_pt_pool = .;
    . += 4096 * BOOT_PT_PAGES;

    boot_uart_pt = .;
    . += 4096;

    boot_pg_end = .;

    . = ALIGN(4K);

    boot_stack = .;
    . += STACK_SIZE * MAX_HARTS;

    . = ALIGN(4K);

    _boot_scratch_end = .;
    _boot_end = .;

    /*
     * High-half kernel.
     */
    _kernel_load_start = .;
    . += KERNEL_OFFSET;

    _kernel_start = .;

    .text : AT(ADDR(.text) - KERNEL_OFFSET) {
        _stext = .;

        KEEP(*(.text.highboot))
        KEEP(*(.text.highboot.*))

        KEEP(*(.text.rust_main_real))
        KEEP(*(.text.rust_main_real.*))

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
    _tlb_refill_phys   = __tlb_refill  - KERNEL_OFFSET;

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