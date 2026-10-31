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

    /* ---------- 分词：strtok / strtok_r / strsep（2026-10-31 补） ----------
     * 这三个的差别全在边界上，是"编得过、用起来错"的典型：
     *   strtok  连续分隔符只算一个，空字段被吞掉，不可重入
     *   strtok_r 可重入，两路迭代互不干扰
     *   strsep  空字段保留成 ""，要数字段个数的解析必须用它 */
    char t1[] = "a,b,c";
    CHECK_STREQ(strtok(t1, ","), "a", "strtok 第一个 token");
    CHECK_STREQ(strtok(0, ","), "b", "strtok 继续切");
    CHECK_STREQ(strtok(0, ","), "c", "strtok 最后一个 token");
    CHECK(strtok(0, ",") == 0, "strtok 切完返回 NULL");

    char t2[] = ",,a,,";
    CHECK_STREQ(strtok(t2, ","), "a", "strtok 跳过前导与连续分隔符");
    CHECK(strtok(0, ",") == 0, "strtok 跳过尾随分隔符后结束");

    char t3[] = "";
    CHECK(strtok(t3, ",") == 0, "strtok 空串返回 NULL");
    char t4[] = ",,";
    CHECK(strtok(t4, ",") == 0, "strtok 全是分隔符也返回 NULL");

    char t5[] = "a::b";
    char *rest = t5;
    CHECK_STREQ(strsep(&rest, ":"), "a", "strsep 第一个字段");
    CHECK_STREQ(strsep(&rest, ":"), "", "strsep 保留空字段（与 strtok 的关键区别）");
    CHECK_STREQ(strsep(&rest, ":"), "b", "strsep 最后一个字段");
    CHECK(strsep(&rest, ":") == 0, "strsep 结束返回 NULL");

    char t6[] = "1,2;3;4";
    char *sa = 0, *sb = 0;
    CHECK_STREQ(strtok_r(t6, ",", &sa), "1", "strtok_r 第一个 token");
    CHECK(strtok_r(0, ";", &sb) == 0, "strtok_r 另一路 saveptr 初值为 NULL 时找不到");
    CHECK_STREQ(strtok_r(0, ",", &sa), "2;3;4", "strtok_r 第一路迭代没被影响");
    CHECK(strtok_r(0, ",", &sa) == 0, "strtok_r 第一路结束");

    /* ---------- memccpy / stpcpy / strndup ---------- */
    char dst[8];
    memset(dst, 'Z', sizeof(dst));
    /* 返回值是 dest 中 c 之后那个字节的地址，不是"拷了几字节" */
    CHECK(memccpy(dst, "abc:def", ':', 8) == dst + 4, "memccpy 返回 c 之后的位置");
    CHECK(dst[3] == ':' && dst[4] == 'Z', "memccpy 遇到 c 就停");
    CHECK(memccpy(dst, "abcdef", ':', 8) == 0, "memccpy 没遇到 c 返回 NULL");
    memset(dst, 'Z', sizeof(dst));
    CHECK(memccpy(dst, "abc:def", ':', 2) == 0, "memccpy 先到 n 就停");
    CHECK(dst[0] == 'a' && dst[1] == 'b' && dst[2] == 'Z', "memccpy 最多写 n 字节");

    char cat[16];
    char *end = stpcpy(cat, "hello");
    CHECK(end == cat + 5 && *end == '\0', "stpcpy 返回指向结尾 NUL 的指针");
    stpcpy(end, " world");
    CHECK_STREQ(cat, "hello world", "stpcpy 接在返回值上可续拼");

    char *nd = strndup("abcdef", 3);
    CHECK(nd != 0 && strcmp(nd, "abc") == 0, "strndup 按 maxlen 截断");
    free(nd);
    char *nd2 = strndup("ab", 10);
    CHECK(nd2 != 0 && strcmp(nd2, "ab") == 0, "strndup 串比 maxlen 短时全拷");
    free(nd2);
    CHECK(strndup(0, 3) == 0, "strndup(NULL) 返回 NULL");

    TEST_END();
}
