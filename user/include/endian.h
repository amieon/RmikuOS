/*
 * endian.h —— 字节序（glibc 风格）
 *
 * 为什么需要它：git 的 compat/bswap.h 在 __BYTE_ORDER / __LITTLE_ENDIAN /
 * __BIG_ENDIAN 三个宏都可见时才走"能确定字节序"的分支，否则直接 #error
 * "Cannot determine endianness"。在 glibc 上这三个宏是 <sys/types.h> 间接
 * 带进来的（bits/endian.h），我们的 sys/types.h 因此也 include 本文件。
 *
 * 名字有两套，别混：
 *   __BYTE_ORDER__ / __ORDER_LITTLE_ENDIAN__   编译器内建（GCC 一定有）
 *   __BYTE_ORDER  / __LITTLE_ENDIAN            glibc 头文件里定义的
 * 我们两个目标架构（riscv64、loongarch64）都是小端，但仍然按内建宏判断，
 * 不写死 —— 将来换大端目标时这里不用改。
 */
#ifndef RK_ENDIAN_H
#define RK_ENDIAN_H

#ifndef __LITTLE_ENDIAN
#define __LITTLE_ENDIAN 1234
#endif
#ifndef __BIG_ENDIAN
#define __BIG_ENDIAN    4321
#endif
#ifndef __PDP_ENDIAN
#define __PDP_ENDIAN    3412
#endif

#if !defined(__BYTE_ORDER)
#  if defined(__BYTE_ORDER__)
#    if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#      define __BYTE_ORDER __BIG_ENDIAN
#    elif __BYTE_ORDER__ == __ORDER_PDP_ENDIAN__
#      define __BYTE_ORDER __PDP_ENDIAN
#    else
#      define __BYTE_ORDER __LITTLE_ENDIAN
#    endif
#  else
#    define __BYTE_ORDER __LITTLE_ENDIAN   /* 兜底：本项目的目标全是小端 */
#  endif
#endif

/* 不带下划线的别名（BSD / 老代码用的那套） */
#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN __LITTLE_ENDIAN
#endif
#ifndef BIG_ENDIAN
#define BIG_ENDIAN    __BIG_ENDIAN
#endif
#ifndef BYTE_ORDER
#define BYTE_ORDER    __BYTE_ORDER
#endif

#endif /* RK_ENDIAN_H */
