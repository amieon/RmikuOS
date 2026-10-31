#ifndef RMIKU_SYS_TYPES_H
#define RMIKU_SYS_TYPES_H

/* POSIX <sys/types.h> —— 只做类型别名, 不引入 fs.h 的内核 struct stat,
 * 避免与 <sys/stat.h> 的 POSIX struct stat 冲突。 */

/* 必须是 "../types.h" 而不是 "types.h"：引号形式先查**本文件所在目录**，
 * 而 sys/ 下正好有个同名的 sys/types.h —— 于是它 include 的是它自己，
 * include guard 已经置位 → 整段跳过 → 父目录的 types.h 从来没进来，
 * <stddef.h> 也就没进来，**size_t 根本没被 typedef**。
 *
 * 症状很隐蔽：只要调用方先 include 过 <stdio.h>/<string.h>（它们会正确带入
 * types.h），size_t 就是好的；只有"单独 include <sys/types.h>"的 TU 才会
 * 撞上。curl 的 AC_CHECK_SIZEOF(size_t) 恰好就是这种 TU，于是四个尺寸全编不过，
 * configure 报 "Failed to find size of size_t"（踩过）。 */
#include "../types.h"    /* usize/isize/size_t */

/* 字节序宏。glibc 的 <sys/types.h> 也是从这里间接给出 __BYTE_ORDER 的 */
#include "../endian.h"

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

typedef unsigned long dev_t;     /* 设备号 */
typedef unsigned long ino_t;     /* inode 号 */
typedef unsigned long nlink_t;   /* 硬链接数（文件 1 / 目录 2） */

#ifdef __cplusplus
}
#endif

#endif /* RMIKU_SYS_TYPES_H */
