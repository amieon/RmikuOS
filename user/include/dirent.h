#ifndef RMIKU_DIRENT_H
#define RMIKU_DIRENT_H

/* POSIX <dirent.h> —— shell.c 的 fsdir 虚拟表用（opendir/readdir/closedir）。
 *
 * 注意：fs.h 里也有一个 struct dirent（内核 getdents 的字节布局），
 * 字段完全不同。这里定义 POSIX 版（d_name），两者绝不能同 TU 混用：
 *   - shell.c / 通用 POSIX 代码  → include 本文件
 *   - VFS / 内核布局使用者        → include fs.h（rmiku_vfs.h 内部）
 * DIR 是不透明类型，实际结构由 rmiku_shims.c 定义。 */

#include "sys/types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct dirent {
    long   d_ino;                 /* 无 inode 概念, 恒 0 */
    off_t  d_off;                 /* 恒 0 */
    unsigned short d_reclen;      /* 恒 0 */
    unsigned char  d_type;        /* 内核 file_type: 1=文件 2=目录（非 DT_*） */
    char d_name[256];
};

typedef struct __rk_DIR DIR;

extern DIR *opendir(const char *dirname);
extern struct dirent *readdir(DIR *dirp);
extern int closedir(DIR *dirp);

#ifdef __cplusplus
}
#endif

#endif /* RMIKU_DIRENT_H */
