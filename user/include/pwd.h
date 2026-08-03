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

#ifdef __cplusplus
}
#endif

#endif /* RMIKU_PWD_H */
