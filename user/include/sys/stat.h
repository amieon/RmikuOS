#ifndef RMIKU_SYS_STAT_H
#define RMIKU_SYS_STAT_H

/* POSIX <sys/stat.h> —— 方案 A 统一后是 fs.h 的薄壳。
 * struct stat / stat / lstat / fstat / mkdir / S_IF* / S_IS* / 权限宏
 * 全部由 fs.h 定义（用户态唯一结构, stat() 内部完成内核布局翻译）。 */

#include "fs.h"

#endif /* RMIKU_SYS_STAT_H */
