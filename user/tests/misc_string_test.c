#include "test.h"

/* 杂项:C 字符串库函数 */
int main() {
    TEST_START("misc_string");

    const char *s1 = "hello";
    const char *s2 = "hello";
    const char *s3 = "hellp";

    CHECK_EQ(strlen(""), 0, "strlen 空串为 0");
    CHECK_EQ(strlen(s1), 5, "strlen 正常");
    CHECK_EQ(strcmp(s1, s2), 0, "strcmp 相同串返回 0");
    CHECK(strcmp(s1, s3) != 0, "strcmp 不同串非 0");
    CHECK_EQ(strncmp(s1, s3, 4), 0, "strncmp 前 4 字符相同");

    char buf[32];
    strcpy(buf, s1);
    CHECK_STREQ(buf, s1, "strcpy 复制正确");
    strcat(buf, "!");
    CHECK_STREQ(buf, "hello!", "strcat 拼接正确");

    char *p = strchr(s1, 'l');
    CHECK(p != 0 && *p == 'l', "strchr 找到字符");
    CHECK(strchr(s1, 'z') == 0, "strchr 找不到返回 NULL");

    char mem[8];
    memset(mem, 0xAB, sizeof(mem));
    CHECK(mem[0] == (char)0xAB && mem[7] == (char)0xAB, "memset 填充正确");
    memcpy(mem, s1, strlen(s1) + 1);   /* 连同 NUL 一起复制 */
    CHECK_STREQ(mem, "hello", "memcpy 复制正确");

    TEST_END();
}
