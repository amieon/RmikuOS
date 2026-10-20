/*
 * grp.h —— 组数据库
 *
 * 与 pwd.h 同样的处境：账户库是 /var/etc/group + /etc/group 的文本文件，
 * 由 accounts.h 那套解析。这里先只提供结构体与"查不到"的实现，
 * 让移植代码编得过；真正需要按名字查组时再接到账户库上。
 */
#ifndef RK_GRP_H
#define RK_GRP_H

#include "sys/types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct group {
    char  *gr_name;
    char  *gr_passwd;
    gid_t  gr_gid;
    char **gr_mem;
};

static inline struct group *getgrnam(const char *name) { (void)name; return (struct group *)0; }
static inline struct group *getgrgid(gid_t gid)        { (void)gid;  return (struct group *)0; }

#ifdef __cplusplus
}
#endif

#endif /* RK_GRP_H */
