
/* exit: POSIX 退出——先冲刷标准流缓冲再退出, 否则短输出(<BUFSIZ)且无换行
 * 会滞留缓冲, 进程退出(直接 syscall)时丢失。
 * weak: runmain.o(-run) 自带强 exit 时让位。 */
#include "../include/file.h"
#include "../include/stdio.h"

/* 三个标准流的唯一定义（file.h 里是 extern 声明）。
 * FILE 池(fopen 用)仍每 TU 一份, 但 stdout/stderr 必须跨 TU 共享。 */
FILE _stdin  = {0, {0}, 0, 0, _F_READ, -1};
FILE _stdout = {1, {0}, 0, 0, _F_WRITE, -1};
FILE _stderr = {2, {0}, 0, 0, _F_WRITE | _F_UNBUF, -1};

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

/* gcc 内建原子 __sync_*: TCC 不支持这些内建 -> 需要真实符号。
 * RmikuOS 单核, 用原子指令实现即可(riscv64: amoswap.w.aq + fence; loongarch64: amswap_db.w)。
 * lock.h 的 spin_lock/spin_unlock 依赖它们。 */
int __sync_lock_test_and_set(int *ptr, int val) {
    int old;
#ifdef USER_ARCH_RISCV64
    __asm__ volatile("amoswap.w.aq %0, %1, (%2)"
                     : "=r"(old) : "r"(val), "r"(ptr) : "memory");
#else
    __asm__ volatile("amswap_db.w %0, %1, %2"
                     : "=r"(old) : "r"(val), "r"(ptr) : "memory");
#endif
    return old;
}
void __sync_synchronize(void) {
    __asm__ volatile("fence rw,rw" ::: "memory");
}
void __sync_lock_release(int *ptr) {
    __asm__ volatile("" ::: "memory");
    *ptr = 0;
}

/* isatty: 0/1/2(标准流)是终端, 其余 fd 视为文件(kilo 等编辑器需要) */
int isatty(int fd) {
    return fd >= 0 && fd <= 2;
}

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