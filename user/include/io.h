#pragma once
#ifdef __cplusplus
extern "C" {
#endif


#include "syscall.h"
#include "flag.h"
#include "strutil.h"
#include <stdarg.h>   /* open() 变参取 mode */

static inline isize write(int fd, const char *buf, usize len) {
    return syscall3(SYS_WRITE, (usize)fd, (usize)buf, len);
}

static inline isize read(int fd, char *buf, usize len) {
    return syscall3(SYS_READ, (usize)fd, (usize)buf, len);
}

static inline void put_char(char ch) {
    write(1, &ch, 1);
}


static inline isize create2(const char *path, usize len) {
    return syscall3(SYS_CREATE, (usize)path, len, 0);
}

static inline isize create(const char *path) {
    return create2(path, strlen(path));
}

static inline isize open2(const char *path, usize len, usize flags) {
    /* 内核 SYS_OPEN 现在 4 参(path,len,flags,mode); 必须 syscall6 传 mode=0,
     * 否则 a3 寄存器残留垃圾会被当 mode。 */
    return syscall6(SYS_OPEN, (usize)path, len, flags, 0, 0, 0);
}

/* open 变参: POSIX 的 mode 只在 O_CREAT 时有效; 调用方 O_CREAT 时必须传 mode。 */
static inline isize open(const char *path, usize flags, ...) {
    usize mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, usize);
        va_end(ap);
    }
    return syscall6(SYS_OPEN, (usize)path, (usize)strlen(path), flags, mode, 0, 0);
}

static inline isize open_create(const char *path, usize flags) {
    return open2(path, strlen(path), flags|O_CREAT);
}


static inline isize close(int fd) {
    return syscall3(SYS_CLOSE, (usize)fd, 0, 0);
}

/* 终端回显开关(内核 line discipline): 1=开 0=关。
 * RmikuOS shell 自带行编辑器, 提示符期间关掉(自己回显);
 * 执行外部交互命令前打开(子进程依赖内核回显)。 */
static inline isize set_echo(int on) {
    return syscall3(SYS_SET_ECHO, (usize)on, 0, 0);
}
#ifdef __cplusplus
}
#endif
