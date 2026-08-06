#ifndef RMIKU_DIRENT_H
#define RMIKU_DIRENT_H

/* POSIX <dirent.h> —— opendir/readdir/closedir 的 static inline 实现。
 *
 * 基于 SYS_GETDENTS 的真实目录遍历（逻辑与 sqlite3 的 rmiku_shims.c 原实现
 * 一致, 已上移至此, 让所有用户程序都能直接用 POSIX 目录遍历接口）。
 * struct dirent 由 fs.h 定义（内核 64 字节布局, d_name 为 POSIX 别名,
 * 名字按 name_len 取, 不带 NUL, 由 readdir 拷贝补齐）。
 */

#include "fs.h"
#include "io.h"
#include "string.h"
#include "stdlib.h"
#include "flag.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct __rk_DIR DIR;

struct __rk_DIR {
    int fd;
    char kbuf[1024];          /* 内核格式条目缓冲 */
    isize klen;               /* getdents 返回的字节数 */
    isize kpos;               /* 已消费偏移 */
    struct dirent cur;        /* 当前 POSIX 条目（readdir 返回其地址） */
};

static inline DIR *opendir(const char *dirname) {
    int fd = (int)open(dirname, O_RDONLY);
    if (fd < 0) return (DIR *)0;
    struct __rk_DIR *d = (struct __rk_DIR *)malloc(sizeof(struct __rk_DIR));
    if (!d) { close(fd); return (DIR *)0; }
    d->fd = fd;
    d->klen = 0;
    d->kpos = 0;
    return (DIR *)d;
}

static inline struct dirent *readdir(DIR *dirp) {
    if (!dirp) return (struct dirent *)0;
    struct __rk_DIR *d = (struct __rk_DIR *)dirp;
    for (;;) {
        if (d->kpos >= d->klen) {
            /* 缓冲耗尽, 再向内核取一批 */
            d->klen = getdents(d->fd, (struct dirent *)d->kbuf, sizeof(d->kbuf));
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

static inline int closedir(DIR *dirp) {
    if (!dirp) return -1;
    struct __rk_DIR *d = (struct __rk_DIR *)dirp;
    close(d->fd);
    free(dirp);
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* RMIKU_DIRENT_H */
