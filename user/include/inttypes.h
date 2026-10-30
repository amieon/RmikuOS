/*
 * inttypes.h —— 定长整型的格式化宏（C99）
 *
 * gcc(riscv64-unknown-elf) 自带 <stdint.h>，但 inttypes.h 属于 libc，裸机工具链
 * 不带 —— TCC 的 elf.h、git 的 archive-tar.c（"%011"PRIoMAX）都要用，这里补齐。
 *
 * 两条约定：
 *   1. 本项目的 vsnprintf 只保证支持 %lld/%llu/%llx（实现里有 is_ll 分支），
 *      所以 64 位一律用 "ll" 前缀。LP64 下 int64_t 其实是 long，严格说该用 "l"，
 *      但两者在 ABI 里都是 64 位、同一条寄存器通道，能用；改成 "l" 得先确认
 *      printf 的 %ld 分支没问题，收益不抵风险。
 *   2. 8/16 位不用 hh/h 前缀：C 的可变参数会把它们提升成 int，"d" 就够了
 *      （glibc 用 "hhd"/"hd"，纯属保险）。
 *
 * 表格按 C99 补全：d/i/o/u/x/X 六个转换 × 8/16/32/64/MAX/PTR。
 * 少写一个，下游就是一句"expected ')' before 'PRIoMAX'"这种看不懂的报错。
 */
#ifndef _INTTYPES_H
#define _INTTYPES_H

#include <stdint.h>
#include "stdlib.h"   /* strtoll/strtoull：inttypes 的转换函数建在它们之上 */

typedef int64_t  intmax_t;
typedef uint64_t uintmax_t;
typedef struct { intmax_t quot, rem; } imaxdiv_t;

/* C99 把这三个宏划给 <stdint.h>，gcc 自带的会定义，TCC 的不一定，兜底一份。
 * 必须真的存在：OpenSSL apps/lib/opt.c 的 guard 就是 `defined(INTMAX_MAX)`，
 * 少一个宏它整段 strtoimax 直接走 #else 的 opt_long fallback，函数补了也用不上。 */
#ifndef INTMAX_MAX
#define INTMAX_MAX  9223372036854775807LL
#define INTMAX_MIN  (-INTMAX_MAX - 1LL)
#define UINTMAX_MAX 18446744073709551615ULL
#endif

#define INTMAX_C(v)  v##LL
#define UINTMAX_C(v) v##ULL

/* ---- 转换 ---- */
/* 注意：这里的 strtoimax/strtoumax 只是宽度别名（LP64 下 intmax_t 就是 long），
 * 真正干活的 rk_strto_core 在 stdlib.h，溢出/endp 语义都在那边统一。 */
static inline intmax_t strtoimax(const char *s, char **e, int b) {
    return (intmax_t)strtoll(s, e, b);
}

static inline uintmax_t strtoumax(const char *s, char **e, int b) {
    return (uintmax_t)strtoull(s, e, b);
}

/* ---- 绝对值 / 整除（C99 要求，缺了就是 imaxabs 隐式声明） ---- */
static inline intmax_t imaxabs(intmax_t x) {
    return x < 0 ? -x : x;
}

static inline imaxdiv_t imaxdiv(intmax_t num, intmax_t den) {
    imaxdiv_t r;
    r.quot = num / den;
    r.rem  = num % den;
    return r;
}

/* ---- 8 位 ---- */
#define PRId8   "d"
#define PRIi8   "i"
#define PRIo8   "o"
#define PRIu8   "u"
#define PRIx8   "x"
#define PRIX8   "X"

/* ---- 16 位 ---- */
#define PRId16  "d"
#define PRIi16  "i"
#define PRIo16  "o"
#define PRIu16  "u"
#define PRIx16  "x"
#define PRIX16  "X"

/* ---- 32 位 ---- */
#define PRId32  "d"
#define PRIi32  "i"
#define PRIo32  "o"
#define PRIu32  "u"
#define PRIx32  "x"
#define PRIX32  "X"

/* ---- 64 位 ---- */
#define PRId64  "lld"
#define PRIi64  "lli"
#define PRIo64  "llo"
#define PRIu64  "llu"
#define PRIx64  "llx"
#define PRIX64  "llX"

/* ---- intmax_t / uintmax_t ---- */
#define PRIdMAX "lld"
#define PRIiMAX "lli"
#define PRIoMAX "llo"
#define PRIuMAX "llu"
#define PRIxMAX "llx"
#define PRIXMAX "llX"

/* ---- intptr_t / uintptr_t（rv64: long） ---- */
#define PRIdPTR "ld"
#define PRIiPTR "li"
#define PRIoPTR "lo"
#define PRIuPTR "lu"
#define PRIxPTR "lx"
#define PRIXPTR "lX"

/* ---- scanf 侧（strtoimax 用不到，但标准要求存在） ---- */
#define SCNd8   "d"
#define SCNi8   "i"
#define SCNo8   "o"
#define SCNu8   "u"
#define SCNx8   "x"
#define SCNd16  "d"
#define SCNi16  "i"
#define SCNo16  "o"
#define SCNu16  "u"
#define SCNx16  "x"
#define SCNd32  "d"
#define SCNi32  "i"
#define SCNo32  "o"
#define SCNu32  "u"
#define SCNx32  "x"
#define SCNd64  "lld"
#define SCNi64  "lli"
#define SCNo64  "llo"
#define SCNu64  "llu"
#define SCNx64  "llx"
#define SCNdMAX "lld"
#define SCNiMAX "lli"
#define SCNoMAX "llo"
#define SCNuMAX "llu"
#define SCNxMAX "llx"
#define SCNdPTR "ld"
#define SCNiPTR "li"
#define SCNuPTR "lu"
#define SCNxPTR "lx"

#endif /* _INTTYPES_H */
