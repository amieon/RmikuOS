/* ============================================================================
 * rmiku_shims.c —— RmikuOS libc 缺失符号的补充实现（sqlite3 shell 专用）
 *
 * 原则：
 *   - 有 syscall 支撑的做真实实现：
 *       isatty        → 标准流 fd 0/1/2 视为 tty（shell 据此决定交互模式）
 *       access        → 已移至 fs.h（内核 SYS_ACCESS：check_access + 真实 uid/gid）
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
 * getpwuid —— 无 passwd 数据库, 诚实返回 NULL（shell 的 ~ 展开退回 getenv）。
 * -------------------------------------------------------------------------- */
struct passwd *getpwuid(uid_t uid) {
    (void)uid;
    return (struct passwd *)0;
}

