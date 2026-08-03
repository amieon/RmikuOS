
/* exit: POSIX 退出——先冲刷标准流缓冲再退出, 否则短输出(<BUFSIZ)且无换行
 * 会滞留缓冲, 进程退出(直接 syscall)时丢失。
 * weak: runmain.o(-run) 自带强 exit 时让位。 */
 
#include "../include/file.h"
#include "../include/stdio.h"

void exit(int code) __attribute__((weak));
void exit(int code) {
    fflush(stdout);
    fflush(stderr);
    syscall3(SYS_EXIT, (usize)code, 0, 0);
    for (;;) {}
}

/* errno: POSIX 全局错误码(errno.h 已声明, 这里提供定义)。
 * weak: lua.c 等移植代码自带强定义时优先, 否则用这个(TCC 等)。 */
int errno __attribute__((weak)) = 0;

void *memset(void *s, int c, unsigned long n) {
    unsigned char *p = (unsigned char *)s;
    while (n--) {
        *p++ = (unsigned char)c;
    }
    return s;
}

void *memcpy(void *dst, const void *src, unsigned long n) {
    char *d = (char *)dst;
    const char *s = (const char *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dst;
}