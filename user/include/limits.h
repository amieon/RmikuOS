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

#ifndef LLONG_MIN
#define LLONG_MIN (-9223372036854775807LL - 1LL)
#define LLONG_MAX 9223372036854775807LL
#define ULLONG_MAX 18446744073709551615ULL
#endif

#define USHRT_MAX 65535          /* 新增 */
#ifdef __CHAR_UNSIGNED__          /* 新增：CHAR_MIN/CHAR_MAX 随 char 符号性 */
#define CHAR_MIN 0
#define CHAR_MAX UCHAR_MAX
#else
#define CHAR_MIN SCHAR_MIN
#define CHAR_MAX SCHAR_MAX
#endif
#define MB_LEN_MAX 1              /* 新增：无多字节字符 */


#endif /* RMIKU_LIMITS_H */
