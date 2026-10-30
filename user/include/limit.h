#ifndef LIMITS_H
#define LIMITS_H
#define CHAR_BIT 8
#define SCHAR_MIN (-128)
#define SCHAR_MAX 127
#define UCHAR_MAX 255
#define SHRT_MIN (-32768)
#define SHRT_MAX 32767
#define INT_MAX 2147483647
#define UINT_MAX 4294967295U
/* MIN 一律写成 -MAX - 1，不要直接写负的最小值字面量：
 * `-9223372036854775808L` 里那个字面量本身就超出 long 范围，gcc 会判成
 * unsigned long 并报 "integer constant is so large that it is unsigned"，
 * 于是 LONG_MIN 变成**无符号**的 2^63 —— `v == LONG_MIN` 这种比较会把 v 悄悄
 * 提升成 unsigned，类型语义整体跑偏（OpenSSL apps/s_client.c:352 就是这么报的）。
 * 这也是 glibc <limits.h> 的写法。 */
#define LONG_MAX 9223372036854775807L
#define LONG_MIN (-LONG_MAX - 1L)
#define INT_MIN (-INT_MAX - 1)
#define ULONG_MAX 18446744073709551615UL
#endif