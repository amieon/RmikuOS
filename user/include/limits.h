#ifndef RMIKU_LIMITS_H
#define RMIKU_LIMITS_H

/* POSIX <limits.h> —— 基本整型范围在 limit.h, 这里补路径/名字长度上限。 */

#include "limit.h"

#ifndef PATH_MAX
#define PATH_MAX 512      /* 与 rmiku_vfs.h 的 RMKU_MAX_PATH 一致 */
#endif

#ifndef NAME_MAX
#define NAME_MAX 56       /* 与 fs.h struct dirent.name[56] 一致 */
#endif

#endif /* RMIKU_LIMITS_H */
