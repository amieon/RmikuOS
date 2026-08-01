#ifndef RMIKU_DIRENT_H
#define RMIKU_DIRENT_H

/* POSIX <dirent.h> —— 方案 A 统一后是 fs.h 的薄壳。
 * struct dirent 由 fs.h 定义（保持内核 getdents 64 字节布局, 但含
 * d_name 联合别名）。opendir/readdir/closedir 由 rmiku_shims.c 实现。 */

#include "fs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct __rk_DIR DIR;

extern DIR *opendir(const char *dirname);
extern struct dirent *readdir(DIR *dirp);
extern int closedir(DIR *dirp);

#ifdef __cplusplus
}
#endif

#endif /* RMIKU_DIRENT_H */
