#ifndef RMIKU_SYS_TYPES_H
#define RMIKU_SYS_TYPES_H

/* POSIX <sys/types.h> —— 只做类型别名, 不引入 fs.h 的内核 struct stat,
 * 避免与 <sys/stat.h> 的 POSIX struct stat 冲突。 */

#include "types.h"    /* usize/isize/size_t */

#ifdef __cplusplus
extern "C" {
#endif

typedef long          ssize_t;   /* LP64: long 与 isize 同宽 */
typedef long          off_t;
typedef unsigned int  mode_t;
typedef unsigned int  uid_t;
typedef unsigned int  gid_t;
#ifndef _RMIKU_TIME_T_DEFINED     /* time.h 也定义了 time_t, 避免重复 */
#define _RMIKU_TIME_T_DEFINED
typedef long          time_t;
#endif
typedef long          pid_t;

#ifdef __cplusplus
}
#endif

#endif /* RMIKU_SYS_TYPES_H */
