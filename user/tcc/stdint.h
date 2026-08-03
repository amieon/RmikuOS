/* stdint.h —— RmikuOS/TCC 专用最小版。
 *
 * gcc 编译用户程序用编译器内置的 stdint.h(无需此文件);
 * 本文件只进 TCC 的 sysinclude(/usr/lib/tcc/include), 供 TCC 编译用户程序时
 * math.h 等 include <stdint.h> 使用(裸机工具链不提供 libc 的 stdint.h)。
 * 布局: riscv64/lp64 —— int=32, long=64, long long=64。
 */
#ifndef _STDINT_H
#define _STDINT_H

typedef signed char        int8_t;
typedef short              int16_t;
typedef int                int32_t;
typedef long               int64_t;
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long      uint64_t;
typedef long               intptr_t;
typedef unsigned long      uintptr_t;
typedef long               intmax_t;
typedef unsigned long      uintmax_t;

#define INT8_MIN   (-128)
#define INT16_MIN  (-32767 - 1)
#define INT32_MIN  (-2147483647 - 1)
#define INT64_MIN  (-9223372036854775807L - 1)
#define INT8_MAX   127
#define INT16_MAX  32767
#define INT32_MAX  2147483647
#define INT64_MAX  9223372036854775807L
#define UINT8_MAX  255
#define UINT16_MAX 65535
#define UINT32_MAX 4294967295U
#define UINT64_MAX 18446744073709551615UL

#endif /* _STDINT_H */
