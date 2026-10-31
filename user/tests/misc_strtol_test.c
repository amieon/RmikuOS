#include "test.h"
#include <stdlib.h>
#include <inttypes.h>
#include <errno.h>
#include <limits.h>

/*
 * strto* 家族：C99 语义里最容易漏的三件事
 *   1. 溢出饱和 + errno=ERANGE（不能静默回绕）
 *   2. 一个数字都没转换到 → endp 指回**起点**
 *   3. base 非法 → errno=EINVAL
 *
 * OpenSSL apps/lib/opt.c 的 opt_long / opt_intmax / opt_uintmax 三条全依赖：
 *   if (*endp || endp == value || ((m == INTMAX_MAX || m == INTMAX_MIN) && errno == ERANGE))
 * 缺一条的症状不是崩溃，而是「命令行里的数字被静默解析成 0 或别的值」，
 * 例如 -iter 0x10000000000000000 回绕成 0、-iter - 被当成 0 接受。
 */
int main(void) {
    TEST_START("misc_strtol");

    char *e;
    long v;

    /* ---- 基本解析 ---- */
    CHECK_EQ(strtol("123", &e, 10), 123, "strtol 十进制");
    CHECK(*e == '\0', "strtol endp 到串尾");
    CHECK_EQ(strtol("-123", 0, 10), -123, "strtol 负数");
    CHECK_EQ(strtol("+42", 0, 10), 42, "strtol 显式正号");
    CHECK_EQ(strtol("0x1f", 0, 0), 31, "base=0 认 0x 十六进制");
    CHECK_EQ(strtol("010", 0, 0), 8, "base=0 前导零判八进制");
    CHECK_EQ(strtol("0x1f", 0, 16), 31, "base=16 显式时也认 0x");
    CHECK_EQ(strtol("z", 0, 36), 35, "base=36 认字母");
    CHECK_EQ(strtol("0", 0, 0), 0, "单个 0");
    CHECK_EQ(strtol("-0", 0, 10), 0, "负零");

    /* ---- 完整六字符空白集（旧实现漏了 \r） ---- */
    CHECK_EQ(strtol(" \t\n\r\v\f42", 0, 10), 42, "六种空白都跳过");

    /* ---- endp 语义：没转换到数字要指回起点 ---- */
    e = NULL;
    v = strtol("-", &e, 10);
    CHECK_EQ(v, 0, "只有符号 → 返回 0");
    CHECK(e != NULL && *e == '-', "只有符号 → endp 指回起点（旧实现会跳过 '-'）");

    e = NULL;
    v = strtol("  abc", &e, 10);
    CHECK_EQ(v, 0, "无数字 → 返回 0");
    CHECK(e != NULL && *e == ' ', "无数字 → endp 指回起点（不是跳过空白后）");

    e = NULL;
    v = strtol("0x", &e, 0);
    CHECK_EQ(v, 0, "\"0x\" 后面没有十六进制数字");
    CHECK(e != NULL && *e == 'x', "\"0x\" 的 endp 停在 x（'0' 本身已转换）");

    e = NULL;
    v = strtol("08", &e, 0);
    CHECK_EQ(v, 0, "\"08\" 按八进制只吃到 0");
    CHECK(e != NULL && *e == '8', "\"08\" 的 endp 停在 8");

    e = NULL;
    v = strtol("0b101", &e, 0);
    CHECK_EQ(v, 0, "base=0 不认 0b 前缀");
    CHECK(e != NULL && *e == 'b', "\"0b101\" 的 endp 停在 b");

    e = NULL;
    CHECK_EQ(strtol("42xyz", &e, 10), 42, "尾随垃圾不影响值");
    CHECK(e != NULL && *e == 'x', "endp 停在第一个非数字处");

    /* ---- 有符号溢出：饱和 + ERANGE ---- */
    errno = 0;
    CHECK(strtoll("9223372036854775808", &e, 10) == LLONG_MAX, "正溢出饱和到 LLONG_MAX");
    CHECK_EQ(errno, ERANGE, "正溢出置 ERANGE");

    errno = 0;
    CHECK(strtoll("-9223372036854775809", &e, 10) == LLONG_MIN, "负溢出饱和到 LLONG_MIN");
    CHECK_EQ(errno, ERANGE, "负溢出置 ERANGE");

    errno = 0;
    CHECK(strtoll("0x10000000000000000", &e, 0) == LLONG_MAX, "十六进制溢出饱和");
    CHECK_EQ(errno, ERANGE, "十六进制溢出置 ERANGE");

    errno = 0;
    CHECK(strtoll("9223372036854775807", &e, 10) == LLONG_MAX, "LLONG_MAX 本身不溢出");
    CHECK_EQ(errno, 0, "LLONG_MAX 不置 errno");

    errno = 0;
    CHECK(strtoll("-9223372036854775808", &e, 10) == LLONG_MIN, "LLONG_MIN 本身不溢出");
    CHECK_EQ(errno, 0, "LLONG_MIN 不置 errno（旧实现靠 UB 取巧）");

    /* ---- 无符号溢出 ---- */
    errno = 0;
    CHECK(strtoull("18446744073709551615", &e, 10) == ULLONG_MAX, "ULLONG_MAX 本身");
    CHECK_EQ(errno, 0, "ULLONG_MAX 不置 errno");

    errno = 0;
    CHECK(strtoull("18446744073709551616", &e, 10) == ULLONG_MAX, "无符号溢出饱和");
    CHECK_EQ(errno, ERANGE, "无符号溢出置 ERANGE");

    CHECK(strtoull("-1", 0, 10) == ULLONG_MAX, "无符号 \"-1\" → 全 1（C99 明文）");
    CHECK(strtoull("-0", 0, 10) == 0, "无符号 \"-0\" → 0");

    /* ---- 非法 base ---- */
    errno = 0;
    e = NULL;
    CHECK_EQ(strtol("123", &e, 1), 0, "base=1 返回 0");
    CHECK_EQ(errno, EINVAL, "base=1 置 EINVAL（POSIX/glibc 语义）");
    CHECK(e != NULL && *e == '1', "base 非法时 endp 指回起点");

    errno = 0;
    CHECK_EQ(strtol("123", &e, 37), 0, "base=37 返回 0");
    CHECK_EQ(errno, EINVAL, "base=37 置 EINVAL");

    /* ---- inttypes 的转换与运算 ---- */
    CHECK_EQ(strtoimax("-123", 0, 10), -123, "strtoimax 基本");
    CHECK(strtoumax("18446744073709551615", 0, 10) == UINTMAX_MAX, "strtoumax 上界");
    errno = 0;
    CHECK(strtoimax("9223372036854775808", 0, 10) == INTMAX_MAX, "strtoimax 溢出饱和");
    CHECK_EQ(errno, ERANGE, "strtoimax 溢出置 ERANGE");
    CHECK_EQ(INTMAX_C(123), 123, "INTMAX_C");
    CHECK(UINTMAX_C(123) == (uintmax_t)123, "UINTMAX_C");
    CHECK_EQ(imaxabs(INTMAX_C(-7)), 7, "imaxabs");

    imaxdiv_t d = imaxdiv(INTMAX_C(17), INTMAX_C(5));
    CHECK_EQ(d.quot, 3, "imaxdiv 商");
    CHECK_EQ(d.rem, 2, "imaxdiv 余");

    /* ---- 大输入不能靠回绕"碰巧对" ---- */
    errno = 0;
    CHECK(strtoull("99999999999999999999999999", &e, 10) == ULLONG_MAX, "超长串饱和");
    CHECK_EQ(errno, ERANGE, "超长串置 ERANGE");
    CHECK(*e == '\0', "溢出时 endp 仍停在串尾");

    TEST_END();
}
