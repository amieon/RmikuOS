#if defined(USER_ARCH_RISCV64)
#define USER_UART_PADDR 0x10000000UL
#elif defined(USER_ARCH_LOONGARCH64)
#define USER_UART_PADDR 0x1fe001e0UL
#else
#error unsupported user arch
#endif


static inline void debug_uart_putchar(char ch) {
    volatile unsigned char *uart = (volatile unsigned char *)USER_UART_PADDR;

    /*
     * 16550 UART:
     * LSR offset 5, bit 5 = THR empty.
     */
    while ((uart[5] & 0x20) == 0) {
    }

    uart[0] = (unsigned char)ch;
}

static inline void debug_uart_puts(const char *s) {
    while (*s) {
        debug_uart_putchar(*s++);
    }
}

static inline void debug_uart_puthex(unsigned long x) {
    debug_uart_puts("0x");

    for (int i = 15; i >= 0; i--) {
        unsigned long v = (x >> (i * 4)) & 0xf;
        char ch = (v < 10) ? ('0' + v) : ('a' + v - 10);
        debug_uart_putchar(ch);
    }
}

static inline void debug_uart_nl(void) {
    debug_uart_putchar('\n');
}