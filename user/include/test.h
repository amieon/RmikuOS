#pragma once
/*
 * RmikuOS 统一测试断言库
 *
 * 用法(每个测试文件一个模块,main 里三步):
 *   #include "test.h"
 *   int main() {
 *       TEST_START("proc_fork");          // 1. 声明测试名
 *       CHECK(cond, "描述");               // 2. 各种断言
 *       CHECK_EQ(actual, expect, "描述");
 *       TEST_END();                        // 3. 打印汇总并返回 0/1
 *   }
 *
 * 输出约定(机器可解析):
 *   [TEST] <name> start
 *   [PASS] <描述>                          // 通过:PASS 简短
 *   [FAIL] <描述> (期望 X, 实际 Y)          // 失败:附期望/实际值
 *   [TEST] <name> done: N passed, M failed
 *
 * 退出码约定:0 = 全部通过;1 = 存在失败断言。
 * 失败不中断,继续跑完收集全部失败(便于回归时一次看到所有问题)。
 */
#include "user.h"

static int test_pass_count = 0;
static int test_fail_count = 0;
static const char *test_cur_name = "test";

#define TEST_START(name) do { \
    test_pass_count = 0; \
    test_fail_count = 0; \
    test_cur_name = (name); \
    printf("[TEST] %s start\n", test_cur_name); \
} while (0)

#define TEST_END() do { \
    printf("[TEST] %s done: %d passed, %d failed\n", \
           test_cur_name, test_pass_count, test_fail_count); \
    return (test_fail_count > 0) ? 1 : 0; \
} while (0)

/* 布尔断言 */
#define CHECK(cond, msg) do { \
    if (cond) { \
        test_pass_count++; \
        printf("[PASS] %s\n", msg); \
    } else { \
        test_fail_count++; \
        printf("[FAIL] %s\n", msg); \
    } \
} while (0)

/* 直接失败(用于无法表达的断言) */
#define FAIL(msg) do { \
    test_fail_count++; \
    printf("[FAIL] %s\n", msg); \
} while (0)

/* 比较宏全家桶:每个断言最多求值一次 actual/expect */
#define CHECK_EQ(actual, expect, msg) do { \
    long a = (long)(actual); \
    long e = (long)(expect); \
    if (a == e) { \
        test_pass_count++; \
        printf("[PASS] %s\n", msg); \
    } else { \
        test_fail_count++; \
        printf("[FAIL] %s (期望 %d, 实际 %d)\n", msg, (int)e, (int)a); \
    } \
} while (0)

#define CHECK_NE(actual, expect, msg) do { \
    long a = (long)(actual); \
    long e = (long)(expect); \
    if (a != e) { \
        test_pass_count++; \
        printf("[PASS] %s\n", msg); \
    } else { \
        test_fail_count++; \
        printf("[FAIL] %s (不该等于 %d)\n", msg, (int)e); \
    } \
} while (0)

#define CHECK_LT(actual, limit, msg) do { \
    long a = (long)(actual); \
    long l = (long)(limit); \
    if (a < l) { \
        test_pass_count++; \
        printf("[PASS] %s\n", msg); \
    } else { \
        test_fail_count++; \
        printf("[FAIL] %s (期望 < %d, 实际 %d)\n", msg, (int)l, (int)a); \
    } \
} while (0)

#define CHECK_LE(actual, limit, msg) do { \
    long a = (long)(actual); \
    long l = (long)(limit); \
    if (a <= l) { \
        test_pass_count++; \
        printf("[PASS] %s\n", msg); \
    } else { \
        test_fail_count++; \
        printf("[FAIL] %s (期望 <= %d, 实际 %d)\n", msg, (int)l, (int)a); \
    } \
} while (0)

#define CHECK_GT(actual, limit, msg) do { \
    long a = (long)(actual); \
    long l = (long)(limit); \
    if (a > l) { \
        test_pass_count++; \
        printf("[PASS] %s\n", msg); \
    } else { \
        test_fail_count++; \
        printf("[FAIL] %s (期望 > %d, 实际 %d)\n", msg, (int)l, (int)a); \
    } \
} while (0)

#define CHECK_GE(actual, limit, msg) do { \
    long a = (long)(actual); \
    long l = (long)(limit); \
    if (a >= l) { \
        test_pass_count++; \
        printf("[PASS] %s\n", msg); \
    } else { \
        test_fail_count++; \
        printf("[FAIL] %s (期望 >= %d, 实际 %d)\n", msg, (int)l, (int)a); \
    } \
} while (0)

/* 字符串相等断言 */
#define CHECK_STREQ(actual, expect, msg) do { \
    const char *a = (actual); \
    const char *e = (expect); \
    if (a && e && strcmp(a, e) == 0) { \
        test_pass_count++; \
        printf("[PASS] %s\n", msg); \
    } else { \
        test_fail_count++; \
        printf("[FAIL] %s (期望 \"%s\", 实际 \"%s\")\n", \
               msg, e ? e : "(null)", a ? a : "(null)"); \
    } \
} while (0)
