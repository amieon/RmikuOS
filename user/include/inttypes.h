
/* 最小实现
 *
 * gcc(riscv64-unknown-elf) 内置 <stdint.h>, 但 inttypes.h 属于 libc,
 * 裸机 gcc 不带 —— TCC 源码(elf.h) 需要, 这里补上格式化宏。
 * RmikuOS 的 vsnprintf 支持 %lld/%llu/%llx(有 is_ll 分支)。
 */
#ifndef _INTTYPES_H
#define _INTTYPES_H

#include <stdint.h>

typedef int64_t  intmax_t;
typedef uint64_t uintmax_t;

#define PRId8   "d"
#define PRId16  "d"
#define PRId32  "d"
#define PRId64  "lld"
#define PRIi64  "lli"
#define PRIu8   "u"
#define PRIu16  "u"
#define PRIu32  "u"
#define PRIu64  "llu"
#define PRIx8   "x"
#define PRIx16  "x"
#define PRIx32  "x"
#define PRIx64  "llx"
#define PRIX64  "llX"
#define PRIdMAX "lld"
#define PRIuMAX "llu"
#define PRIxMAX "llx"
#define PRIdPTR "ld"    /* rv64: intptr_t = long */
#define PRIuPTR "lu"
#define PRIxPTR "lx"

#endif /* _INTTYPES_H */
