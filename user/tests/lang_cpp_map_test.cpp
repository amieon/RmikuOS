// C++ 桥接:stdcompat 的 map(my::map,treap 实现)
#include "test.h"
#include "my/stdcompat.h"

extern "C" int main() {
    TEST_START("lang_cpp_map");

    std::map<int, int> m;
    CHECK(m.empty(), "map 初始为空");

    m[10] = 100;
    m[20] = 200;
    m[10] = 999;               /* operator[] 覆盖更新 */
    CHECK_EQ((isize)m.size(), 2, "map 两个 key");
    CHECK_EQ(m[10], 999, "operator[] 覆盖更新生效");
    CHECK(m.count(20) == 1, "count(20) 存在");
    CHECK(m.count(99) == 0, "count(99) 不存在");

    m.erase(10);
    CHECK(m.count(10) == 0, "erase(10) 后不存在");
    CHECK_EQ((isize)m.size(), 1, "erase 后 size=1");

    /* 迭代器遍历 key/value */
    int sum_key = 0, sum_val = 0, n = 0;
    for (std::map<int, int>::iterator it = m.begin(); it != m.end(); ++it) {
        sum_key += it->first;
        sum_val += it->second;
        n++;
    }
    CHECK_EQ(n, 1, "迭代器遍历 1 个元素");
    CHECK(sum_key == 20 && sum_val == 200, "迭代器 key/value 正确");

    TEST_END();
}
