#pragma once

typedef unsigned long usize;
typedef long isize;

#define SYS_EXIT       0
#define SYS_YIELD      1
#define SYS_WRITE      2
#define SYS_GETPID     3
#define SYS_FORK       4
#define SYS_WAITPID    5
#define SYS_SLEEP      6
#define SYS_EXEC       7
#define SYS_READ       8
#define SYS_OPEN       9
#define SYS_CLOSE      10
#define SYS_GETDENTS   11
#define SYS_CHDIR      12
#define SYS_GETCWD     13
#define SYS_STAT       14
#define SYS_FSTAT      15
#define SYS_THREAD_CREATE        16
#define SYS_THREAD_EXIT          17
#define SYS_THREAD_JOIN          18
#define SYS_MMAP                 19
#define SYS_MUNMAP               20
#define SYS_SET_THREAD_TICKETS   21
#define SYS_SET_PROCESS_TICKETS  22
#define SYS_SET_MY_TICKETS       23
#define SYS_GET_THREAD_TICKETS   24
#define SYS_GET_PROCESS_TICKETS  25
#define SYS_GET_MY_TICKETS       26




isize syscall3(usize id, usize a0, usize a1, usize a2);

isize syscall6(usize id, usize a0, usize a1, usize a2, usize a3, usize a4, usize a5);


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

static inline void puts(const char *s) {
    write(1, s, strlen(s));
}

static inline void put_char(char ch) {
    write(1, &ch, 1);
}

static inline isize open2(const char *path, usize len) {
    return syscall3(SYS_OPEN, (usize)path, len, 0);
}

static inline isize open(const char *path) {
    return open2(path, strlen(path));
}

static inline isize close(int fd) {
    return syscall3(SYS_CLOSE, (usize)fd, 0, 0);
}


#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4

static inline void *mmap(usize len, usize prot) {
    isize ret = syscall3(SYS_MMAP, len, prot, 0);

    if (ret < 0) {
        return (void *)-1;
    }

    return (void *)ret;
}

static inline int munmap(void *addr, usize len) {
    return syscall3(SYS_MUNMAP, (usize)addr, len, 0);
}



static inline int set_process_tickets(int pid, int tickets) {
    return syscall3(SYS_SET_PROCESS_TICKETS,  (usize)pid, tickets, 0);
}

static inline int set_my_tickets(int tickets) {
    return syscall3(SYS_SET_MY_TICKETS, tickets, 0, 0);
}

static inline int get_process_tickets(int pid) {
    return syscall3(SYS_GET_PROCESS_TICKETS,  (usize)pid,0 , 0);
}

static inline int get_my_tickets() {
    return syscall3(SYS_GET_MY_TICKETS,0 , 0, 0);
}