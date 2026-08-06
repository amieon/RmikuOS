// C++ 桥接:stdcompat 的 set(my::set,基于 map)
#include "test.h"
#include "my/stdcompat.h"

extern "C" int main() {
    TEST_START("lang_cpp_set");

    std::set<int> s;
    CHECK(s.empty(), "set 初始为空");

    s.insert(3);
    s.insert(1);
    s.insert(2);
    s.insert(3);               /* 重复插入应去重 */
    CHECK_EQ((isize)s.size(), 3, "set 去重后 3 个元素");
    CHECK(s.count(1) == 1, "count(1) 存在");
    CHECK(s.count(9) == 0, "count(9) 不存在");

    s.erase(2);
    CHECK(s.count(2) == 0, "erase(2) 后不存在");
    CHECK_EQ((isize)s.size(), 2, "erase 后 size=2");

    /* 迭代器遍历 */
    int sum = 0, n = 0;
    for (std::set<int>::iterator it = s.begin(); it != s.end(); ++it) {
        sum += *it;
        n++;
    }
    CHECK_EQ(n, 2, "迭代器遍历 2 个元素");
    CHECK_EQ(sum, 4, "剩余元素和 = 1+3");

    s.clear();
    CHECK(s.empty(), "clear 后为空");

    TEST_END();
}
