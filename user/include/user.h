#pragma once

typedef unsigned long usize;
typedef long isize;

#define SYS_EXIT    0
#define SYS_YIELD   1
#define SYS_WRITE   2
#define SYS_GETPID  3
#define SYS_FORK    4
#define SYS_WAITPID 5
#define SYS_SLEEP   6
#define SYS_EXEC 7
#define SYS_READ 8


isize syscall3(usize id, usize a0, usize a1, usize a2);

static inline isize exec(const char *name) {
    return syscall3(SYS_EXEC, (usize)name, strlen(name), 0);
}

static inline isize read(int fd, char *buf, usize len) {
    return syscall3(SYS_READ, (usize)fd, (usize)buf, len);
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

static inline isize write(int fd, const char *buf, usize len) {
    return syscall3(SYS_WRITE, (usize)fd, (usize)buf, len);
}

static inline void puts(const char *s) {
    usize len = strlen(s);
    syscall3(SYS_WRITE, 1, (usize)s, len);
}

static inline void put_char(char ch) {
    syscall3(SYS_WRITE, 1, (usize)&ch, 1);
}

static inline void put_int(long x) {
    char buf[32];
    int i = 0;

    if (x == 0) {
        put_char('0');
        return;
    }

    if (x < 0) {
        put_char('-');
        x = -x;
    }

    while (x > 0) {
        buf[i++] = '0' + (x % 10);
        x /= 10;
    }

    while (i > 0) {
        i--;
        put_char(buf[i]);
    }
}