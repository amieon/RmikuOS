#ifndef RMIKU_PWD_H
#define RMIKU_PWD_H

/* POSIX <pwd.h> —— shell 的 ~ 展开、ls/id 的名字列、git 的 ident.c 都要它。*/
#include "sys/types.h"
#include "io.h"        /* open2 / read / close */
#include "string.h"    /* strlen / strcmp */

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

/* 账户库路径与读入上限（一行 root 约 40 字节，1KB 够几十个用户） */
#define RK_PW_STATE    "/var/etc/passwd"
#define RK_PW_DEFAULT  "/etc/passwd"
#define RK_PW_DB_MAX   1024

/* 只认十进制（passwd 里 uid/gid 就是这个格式），不拉 stdlib.h 的 atoi */
static inline int rk_pw_atoi(const char *s) {
    int v = 0;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return v;
}

/* by_uid=1 按 uid 找，否则按 name 找；找不到返回 NULL。
 * 返回的字段指向本函数内的 static 缓冲，下一次调用会被覆盖。 */
static inline struct passwd *rk_pw_scan(int by_uid, uid_t uid, const char *name) {
    static char db[RK_PW_DB_MAX];
    static char *f[7];
    static struct passwd ent;

    int fd = (int)open2(RK_PW_STATE, strlen(RK_PW_STATE), O_RDONLY);
    if (fd < 0) fd = (int)open2(RK_PW_DEFAULT, strlen(RK_PW_DEFAULT), O_RDONLY);
    if (fd < 0) return (struct passwd *)0;

    int len = 0;
    while (len < RK_PW_DB_MAX - 1) {
        isize n = read(fd, db + len, (usize)(RK_PW_DB_MAX - 1 - len));
        if (n <= 0) break;
        len += (int)n;
    }
    close(fd);
    db[len] = 0;

    char *p = db;
    while (*p) {
        char *nl = p;
        while (*nl && *nl != '\n') nl++;
        int last = (*nl == 0);
        *nl = 0;                       /* 先截行尾，切字段就不会走出本行 */

        if (*p && *p != '#') {
            int nf = 1;
            f[0] = p;
            for (char *q = p; *q; q++) {
                if (*q == ':') { *q = 0; if (nf < 7) f[nf++] = q + 1; }
            }
            if (nf >= 4) {
                int hit = by_uid ? (rk_pw_atoi(f[2]) == (int)uid)
                                 : (name != 0 && strcmp(f[0], name) == 0);
                if (hit) {
                    ent.pw_name   = f[0];
                    ent.pw_passwd = f[1];
                    ent.pw_uid    = (uid_t)rk_pw_atoi(f[2]);
                    ent.pw_gid    = (gid_t)rk_pw_atoi(f[3]);
                    ent.pw_gecos  = (nf > 4) ? f[4] : (char *)"";
                    ent.pw_dir    = (nf > 5) ? f[5] : (char *)"/";
                    ent.pw_shell  = (nf > 6) ? f[6] : (char *)"";
                    return &ent;
                }
            }
        }
        if (last) break;
        p = nl + 1;
    }
    return (struct passwd *)0;
}

static inline struct passwd *getpwuid(uid_t uid) {
    return rk_pw_scan(1, uid, 0);
}

static inline struct passwd *getpwnam(const char *name) {
    if (!name) return (struct passwd *)0;
    return rk_pw_scan(0, 0, name);
}

#ifdef __cplusplus
}
#endif

#endif /* RMIKU_PWD_H */
