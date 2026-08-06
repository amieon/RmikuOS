// C++ 桥接:mystr 字符串工具 + my::string 基础操作
#include "test.h"
#include "my/stdcompat.h"

extern "C" int main() {
    TEST_START("lang_cpp_string");

    /* mystr::strcmp */
    CHECK_EQ(mystr::strcmp("abc", "abc"), 0, "strcmp 相同串为 0");
    CHECK(mystr::strcmp("abc", "abd") < 0, "strcmp 小于");
    CHECK(mystr::strcmp("abd", "abc") > 0, "strcmp 大于");

    /* mystr::str_to_int */
    CHECK_EQ(mystr::str_to_int("123"), 123, "str_to_int 正数");
    CHECK_EQ(mystr::str_to_int("-456"), -456, "str_to_int 负数");
    CHECK_EQ(mystr::str_to_int("0"), 0, "str_to_int 零");

    /* mystr::str_to_double */
    double d1 = mystr::str_to_double("3.14159");
    CHECK(d1 >= 3.141 && d1 <= 3.142, "str_to_double 解析 π 近似");
    CHECK_EQ(mystr::str_to_double("-0.5") == -0.5, 1, "str_to_double 负数");

    /* mystr::split_line */
    char line[] = "hello world 42 3.14";
    char *tokens[16];
    int nt = mystr::split_line(line, tokens, 16);
    CHECK_EQ(nt, 4, "split_line 拆出 4 个 token");
    CHECK_STREQ(tokens[0], "hello", "token[0] 正确");
    CHECK_STREQ(tokens[3], "3.14", "token[3] 正确");

    /* my::string(std::string) 基础 */
    std::string s("hello");
    CHECK_EQ((isize)s.size(), 5, "string size 正确");
    CHECK(!s.empty(), "string 非空");
    CHECK(s[0] == 'h' && s[4] == 'o', "string 下标访问");
    CHECK_STREQ(s.c_str(), "hello", "string c_str 内容正确");

    /* mystr::SimpleMap */
    mystr::SimpleMap<int> smap;
    smap["key1"] = 100;
    smap["key2"] = 200;
    int *v1 = smap.find("key1");
    CHECK(v1 != 0 && *v1 == 100, "SimpleMap 命中");
    CHECK(smap.find("nonexist") == 0, "SimpleMap 未命中返回 NULL");
    CHECK_EQ((isize)smap.size(), 2, "SimpleMap size 正确");

    TEST_END();
}
