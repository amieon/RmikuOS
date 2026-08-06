#include "test.h"

/* 基础 printf:用 snprintf 检查实际输出内容(stdio.h 已移至 include) */
int main() {
    TEST_START("misc_printf");

    char buf[128];

    snprintf(buf, sizeof(buf), "%d", 42);
    CHECK_STREQ(buf, "42", "%d 输出 42");
    snprintf(buf, sizeof(buf), "%d", -99);
    CHECK_STREQ(buf, "-99", "负 %d 输出 -99");
    snprintf(buf, sizeof(buf), "%u", 12345);
    CHECK_STREQ(buf, "12345", "%u 输出 12345");
    snprintf(buf, sizeof(buf), "%x", 255);
    CHECK_STREQ(buf, "ff", "%x 输出 ff");
    snprintf(buf, sizeof(buf), "%c", 'X');
    CHECK_STREQ(buf, "X", "%c 输出 X");
    snprintf(buf, sizeof(buf), "%s", "hello");
    CHECK_STREQ(buf, "hello", "%s 输出 hello");
    snprintf(buf, sizeof(buf), "%%");
    CHECK_STREQ(buf, "%", "%% 输出 %");
    snprintf(buf, sizeof(buf), "%ld", -123456789L);
    CHECK_STREQ(buf, "-123456789", "%ld 输出 -123456789");
    snprintf(buf, sizeof(buf), "a=%d b=%s", 7, "xy");
    CHECK_STREQ(buf, "a=7 b=xy", "混排 a=%d b=%s");

    TEST_END();
}
