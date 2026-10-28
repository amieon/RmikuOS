#pragma once
#ifdef __cplusplus
extern "C" {
#endif


#include "syscall.h"
#include "flag.h"
#include "strutil.h"
#include "errno.h"
#include <stdarg.h>   /* open() 变参取 mode */


/* 内核统一返回 `-errno`，这里翻成 POSIX 的 `-1 + errno`。
 * 注意 `-1` 本身就是 `-EPERM`（EPERM == 1）—— 早先有一个"r == -1 → EIO"
 * 的过渡分支，那会把真实的 EPERM 谎报成 EIO（setsid/fcntl 之类全靠它），
 * 已经删掉：现在负数一律按 errno 解，一个分支就够。 */
static inline isize rk_sys_ret(isize r) {
    if (r < 0 && r >= -4095) { errno = (int)(-r); return -1; }
    return r;
}

static inline isize write(int fd, const char *buf, usize len) {
    return rk_sys_ret(syscall3(SYS_WRITE, (usize)fd, (usize)buf, len));
}

static inline isize read(int fd, char *buf, usize len) {
    return rk_sys_ret(syscall3(SYS_READ, (usize)fd, (usize)buf, len));
}

static inline isize pread(int fd, char *buf, usize count, usize offset) {
    return syscall6(SYS_PREAD, (usize)fd, (usize)buf, count, offset, 0, 0);
}

static inline isize pwrite(int fd, const char *buf, usize count, usize offset) {
    return syscall6(SYS_PWRITE, (usize)fd, (usize)buf, count, offset, 0, 0);
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
    return rk_sys_ret(syscall6(SYS_OPEN, (usize)path, len, flags, 0, 0, 0));
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
    return rk_sys_ret(syscall6(SYS_OPEN, (usize)path, (usize)strlen(path), flags, mode, 0, 0));
}

static inline isize open_create(const char *path, usize flags) {
    return open2(path, strlen(path), flags|O_CREAT);
}

/* openat(dirfd, path, flags, ...) —— 相对目录 fd 打开。
 *
 * dirfd 的三种取值（与 POSIX 一致）：
 *   - 已打开的目录 fd：path 相对它解析
 *   - AT_FDCWD(-100)：相对进程 cwd（此时等价 open）
 *   - path 本身是绝对路径：dirfd 被忽略，哪怕是无效值
 * 内核把这个值当 isize 解释，所以这里必须 (usize)(isize)dirfd 转换——
 * 直接把 -100 当 usize 传会让内核看到 0xFFFFFFFFFFFFFF9C，虽然内核也是按
 * isize 转回来，但显式转换更能表达意图。
 *
 * mode 只在 O_CREAT 时有效，与 open 一致用变参取。 */
static inline isize openat(int dirfd, const char *path, usize flags, ...) {
    usize mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, usize);
        va_end(ap);
    }
    return rk_sys_ret(syscall6(SYS_OPENAT, (usize)(isize)dirfd, (usize)path,
                               (usize)strlen(path), flags, mode, 0));
}


static inline isize close(int fd) {
    return rk_sys_ret(syscall3(SYS_CLOSE, (usize)fd, 0, 0));
}

/* 终端回显开关(内核 line discipline): 1=开 0=关。
 * RmikuOS shell 自带行编辑器, 提示符期间关掉(自己回显);
 * 执行外部交互命令前打开(子进程依赖内核回显)。 */
static inline isize set_echo(int on) {
    return syscall3(SYS_SET_ECHO, (usize)on, 0, 0);
}

/* ---- 内核 Stat 的原始 32 字节布局 + 一次裸 fstat ----
 *
 * 为什么这层的定义放在 io.h 而不是 fs.h：io.h 是最底层（fs.h 反过来 include
 * 它），而 lib/string.c 的 isatty 只需要"文件类型 + 设备号"，却不想把整个
 * fs.h 拉进来 —— fs.h → process.h 里有 `static inline exit()`，会和 string.c
 * 里那个留给 crt0 的 weak exit() 撞成重定义。布局仍是唯一定义点：fs.h 直接
 * `typedef rk_stat_raw_t rk_stat_kernel_t` 复用，不重复写一遍。
 *
 * 与 kernel/src/fs/stat.rs 的 Stat 逐字段对应，改一边必须改另一边。 */
#define RK_STAT_TYPE_CHAR 3   /* 同 kernel 的 STAT_TYPE_CHAR */

/* 设备号（仅字符设备有意义），与 kernel/src/fs/stat.rs 的 DEV_* 一致：
 * isatty 靠"是控制台"而不是"是字符设备"来判定 —— /dev/null 也是字符设备。 */
#define RK_DEV_CONSOLE 0x0500u
#define RK_DEV_NULL    0x0103u

typedef struct rk_stat_raw {
    unsigned char  file_type;   /* @0  */
    unsigned short mode;        /* @2  权限位 */
    unsigned int   uid;         /* @4  */
    unsigned int   gid;         /* @8  */
    usize          size;        /* @16 */
    unsigned int   mtime;       /* @24 epoch 秒 */
    unsigned int   dev;         /* @28 设备号(仅字符设备) */
} rk_stat_raw_t;

/* 成功返回 0，失败返回内核的 -errno（不翻成 -1/errno，留给上层决定） */
static inline int rk_fstat_raw(int fd, rk_stat_raw_t *k) {
    isize r = syscall3(SYS_FSTAT, (usize)fd, (usize)k, 0);
    return (r < 0) ? (int)r : 0;
}

#ifdef __cplusplus
}
#endif
