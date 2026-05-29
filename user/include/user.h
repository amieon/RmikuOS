#pragma once

typedef unsigned long usize;
typedef long isize;

#define SYS_EXIT  0
#define SYS_YIELD 1
#define SYS_WRITE 2

#if defined(USER_ARCH_RISCV64)

static inline isize syscall3(usize id, usize a0, usize a1, usize a2) {
    register usize x10 asm("a0") = a0;
    register usize x11 asm("a1") = a1;
    register usize x12 asm("a2") = a2;
    register usize x17 asm("a7") = id;

    asm volatile (
        "ecall"
        : "+r"(x10)
        : "r"(x11), "r"(x12), "r"(x17)
        : "memory"
    );

    return (isize)x10;
}

#elif defined(USER_ARCH_LOONGARCH64)

static inline isize syscall3(usize id, usize a0, usize a1, usize a2) {
    register usize r4 asm("$r4") = a0;
    register usize r5 asm("$r5") = a1;
    register usize r6 asm("$r6") = a2;
    register usize r11 asm("$r11") = id;

    asm volatile (
        "syscall 0"
        : "+r"(r4)
        : "r"(r5), "r"(r6), "r"(r11)
        : "memory"
    );

    return (isize)r4;
}

#else
#error unsupported user arch
#endif

static inline isize write(int fd, const char *buf, usize len) {
    return syscall3(SYS_WRITE, (usize)fd, (usize)buf, len);
}

static inline isize yield(void) {
    return syscall3(SYS_YIELD, 0, 0, 0);
}

static inline void exit(int code) {
    syscall3(SYS_EXIT, (usize)code, 0, 0);
    for (;;) {}
}

static inline usize strlen(const char *s) {
    usize n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

static inline void puts(const char *s) {
    write(1, s, strlen(s));
}