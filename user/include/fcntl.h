#ifndef RMIKU_FCNTL_H
#define RMIKU_FCNTL_H

/* POSIX <fcntl.h> —— 开放标志与 fcntl 命令号。
 * 注意不 include fs.h（避免把内核 struct stat/dirent 带进 POSIX 代码）。 */

#include "flag.h"

#ifndef O_BINARY
#define O_BINARY 0     /* 无文本/二进制区分, POSIX 语义即 no-op */
#endif

#endif /* RMIKU_FCNTL_H */
