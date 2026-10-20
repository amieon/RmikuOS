#ifndef RMIKU_PWD_H
#define RMIKU_PWD_H

/* POSIX <pwd.h> —— shell.c 在解析 ~ 用户目录时调用 getpwuid()。
 * RmikuOS 无 passwd 数据库, getpwuid() 诚实返回 NULL（rmiku_shims.c 实现）。 */

#include "sys/types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct passwd {
    char  *pw_name;
    char  *pw_passwd;
    uid_t  pw_uid;
    gid_t  pw_gid;
    char  *pw_gecos;
    char  *pw_dir;
    char  *pw_shell;
};

extern struct passwd *getpwuid(uid_t uid);

/* getpwnam：按名字查。与 getpwuid 保持一致——账户库还没有接到 libc 这一层，
 * 所以同样返回 NULL（而不是编一个"看起来像"的条目）。
 * 调用方（git 的 path.c `~user` 展开）对 NULL 有兜底，会退化成不展开。 */
static inline struct passwd *getpwnam(const char *name) {
    (void)name;
    return (struct passwd *)0;
}

#ifdef __cplusplus
}
#endif

#endif /* RMIKU_PWD_H */
