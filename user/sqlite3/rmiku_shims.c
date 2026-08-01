/* ============================================================================
 * rmiku_shims.c —— RmikuOS libc 缺失符号的补充实现（sqlite3 shell 专用）
 *
 * 原则（用户明确要求"尽量不 stub"）：
 *   - 有 syscall 支撑的做真实实现：
 *       isatty        → 标准流 fd 0/1/2 视为 tty（shell 据此决定交互模式）
 *       access        → stat() + 权限位检查, 真实判存在/可读/可写/可执行
 *       opendir/readdir/closedir → 基于 SYS_GETDENTS 的真实目录遍历
 *       errno         → 全局变量定义（errno.h 只声明 extern）
 *   - 无 syscall 支撑的做语义诚实的空实现：
 *       getpwuid      → 无 passwd 数据库, 返回 NULL（与 getenv 无值一致）
 *       symlink       → 无符号链接支持, ENOSYS（writefile() 的死路径, 恒不触发）
 *
 * 注意：本文件 include fs.h（方案 A 统一后 fs.h 是用户态唯一的
 * struct stat / struct dirent 定义, stat() 内部已翻译成 POSIX 布局）。
 * rmiku_vfs.h 不能被本文件 include —— VFS 由 rmiku_vfs.c 单独编译。
 * ==========================================================================*/

#include "syscall.h"
#include "io.h"
#include "string.h"
#include "stdlib.h"      /* malloc/free */
#include "errno.h"
#include "sys/stat.h"    /* → fs.h: POSIX stat() */
#include "dirent.h"      /* → fs.h: struct dirent + DIR 声明 */
#include "pwd.h"

/* errno.h 只声明 extern, 这里给出唯一定义（lua.c 里那个是它自己 TU 的） */
int errno = 0;

/* ----------------------------------------------------------------------------
 * strdup —— 真实实现（malloc + 拷贝）。shell.c 有 5 处调用（~ 展开、
 * .import/.output 文件名、.open 参数等）。
 * -------------------------------------------------------------------------- */
char *strdup(const char *s) {
    if (!s) return 0;
    usize n = strlen(s);
    char *p = (char *)malloc(n + 1);
    if (!p) return 0;
    memcpy(p, s, n + 1);
    return p;
}

/* ----------------------------------------------------------------------------
 * isatty —— shell 用它判断 stdin 是否是交互终端, 决定是否打印提示符/行编辑。
 * RmikuOS 没有 TTY 概念: 标准三流视为终端, 其余 fd 一律不是。
 * -------------------------------------------------------------------------- */
int isatty(int fd) {
    return (fd == 0 || fd == 1 || fd == 2) ? 1 : 0;
}

/* ----------------------------------------------------------------------------
 * access(path, amode) —— F_OK=0 / R_OK=4 / W_OK=2 / X_OK=1
 * 存在性用 stat() 真实判断; 权限位与 stat 的 st_mode 比对。
 * -------------------------------------------------------------------------- */
int access(const char *path, int amode) {
    struct stat st;
    if (stat(path, &st) != 0) { errno = ENOENT; return -1; }
    if (amode == 0) return 0;                      /* F_OK: 只看存在 */
    mode_t need = 0;
    if (amode & 4) need |= S_IRUSR;
    if (amode & 2) need |= S_IWUSR;
    if (amode & 1) need |= S_IXUSR;
    if ((st.st_mode & need) != need) { errno = EACCES; return -1; }
    return 0;
}

/* ----------------------------------------------------------------------------
 * symlink —— 本 OS 无符号链接; 仅供 writefile() 的 S_ISLNK 死分支引用
 * （我们平台 S_ISLNK 恒 0, 此函数不会被真实调用）。返回 ENOSYS。
 * -------------------------------------------------------------------------- */
int symlink(const char *target, const char *linkpath) {
    (void)target; (void)linkpath;
    errno = ENOSYS;
    return -1;
}

/* ----------------------------------------------------------------------------
 * getpwuid —— 无 passwd 数据库, 诚实返回 NULL（shell 的 ~ 展开退回 getenv）。
 * -------------------------------------------------------------------------- */
struct passwd *getpwuid(uid_t uid) {
    (void)uid;
    return (struct passwd *)0;
}

/* ----------------------------------------------------------------------------
 * opendir / readdir / closedir —— 基于 SYS_GETDENTS 的真实目录遍历。
 *
 * 内核 getdents 语义（fs.h 注释 + 用户态 ls.c 验证）:
 *   返回读到的【字节数】; 缓冲内是若干等长 struct dirent（fs.h 布局,
 *   64 字节一个, 名字按 name_len 取, 不带 NUL）。fs.h 的 struct dirent
 *   就是内核布局（含 d_name 别名）, 直接用它解析, 无需翻译副本。
 * -------------------------------------------------------------------------- */
struct __rk_DIR {
    int fd;
    char kbuf[1024];          /* 内核格式条目缓冲 */
    isize klen;               /* getdents 返回的字节数 */
    isize kpos;               /* 已消费偏移 */
    struct dirent cur;        /* 当前 POSIX 条目（readdir 返回其地址） */
};

DIR *opendir(const char *dirname) {
    int fd = (int)open(dirname, O_RDONLY);
    if (fd < 0) return (DIR *)0;
    struct __rk_DIR *d = (struct __rk_DIR *)malloc(sizeof(struct __rk_DIR));
    if (!d) { close(fd); return (DIR *)0; }
    d->fd = fd;
    d->klen = 0;
    d->kpos = 0;
    return (DIR *)d;
}

struct dirent *readdir(DIR *dirp) {
    if (!dirp) return (struct dirent *)0;
    struct __rk_DIR *d = (struct __rk_DIR *)dirp;
    for (;;) {
        if (d->kpos >= d->klen) {
            /* 缓冲耗尽, 再向内核取一批 */
            d->klen = syscall3(SYS_GETDENTS, (usize)d->fd, (usize)d->kbuf,
                               (usize)sizeof(d->kbuf));
            d->kpos = 0;
            if (d->klen <= 0) return (struct dirent *)0;   /* 0=EOF, <0=错误 */
        }
        struct dirent *ke = (struct dirent *)(d->kbuf + d->kpos);
        d->kpos += (isize)sizeof(struct dirent);

        /* 跳过 . 与 .. */
        if (ke->name_len == 1 && ke->name[0] == '.') continue;
        if (ke->name_len == 2 && ke->name[0] == '.' && ke->name[1] == '.') continue;

        int n = ke->name_len;
        if (n > 255) n = 255;
        memcpy(d->cur.d_name, ke->name, (usize)n);
        d->cur.d_name[n] = '\0';
        return &d->cur;
    }
}

int closedir(DIR *dirp) {
    if (!dirp) return -1;
    struct __rk_DIR *d = (struct __rk_DIR *)dirp;
    close(d->fd);
    free(dirp);
    return 0;
}
