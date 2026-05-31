#pragma once

typedef unsigned long usize;
typedef long isize;

#define SYS_EXIT   0
#define SYS_YIELD  1
#define SYS_WRITE  2
#define SYS_GETPID 3
#define SYS_FORK   4
#define SYS_WAITPID 5
#define SYS_SLEEP  6

isize syscall3(usize id, usize a0, usize a1, usize a2);

static inline isize write(int fd, const char *buf, usize len) {
    return syscall3(SYS_WRITE, (usize)fd, (usize)buf, len);
}

static inline isize yield(void) {
    return syscall3(SYS_YIELD, 0, 0, 0);
}

static inline isize getpid(void) {
    return syscall3(SYS_GETPID, 0, 0, 0);
}

static inline isize fork(void) {
    return syscall3(SYS_FORK, 0, 0, 0);
}

static inline isize waitpid(isize pid, int *exit_code) {
    return syscall3(SYS_WAITPID, (usize)pid, (usize)exit_code, 0);
}

static inline isize sleep(usize ticks) {
    return syscall3(SYS_SLEEP, ticks, 0, 0);
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