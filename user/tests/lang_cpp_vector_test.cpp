// C++ 桥接:stdcompat 的 vector(mv::Vector)基本操作
#include "test.h"
#include "my/stdcompat.h"

extern "C" int main() {
    TEST_START("lang_cpp_vector");

    /* 构造 */
    std::vector<int> v1;
    CHECK(v1.empty(), "默认构造为空");
    std::vector<int> v2(5);
    CHECK_EQ((isize)v2.size(), 5, "size 构造 5 元素");
    std::vector<int> v3(3, 7);
    CHECK(v3[0] == 7 && v3[2] == 7, "值构造(3 个 7)");

    /* push_back / pop_back */
    std::vector<int> v4;
    v4.push_back(1); v4.push_back(2); v4.push_back(3);
    CHECK_EQ((isize)v4.size(), 3, "push_back 3 次");
    CHECK(v4.back() == 3, "back 为最后元素");
    v4.pop_back();
    CHECK_EQ((isize)v4.size(), 2, "pop_back 后 size=2");
    CHECK(v4.back() == 2, "pop_back 后 back=2");

    /* reserve / capacity */
    v4.reserve(20);
    CHECK(v4.capacity() >= 20, "reserve 后 capacity 足够");

    /* resize */
    v4.resize(10);
    CHECK_EQ((isize)v4.size(), 10, "resize 扩大");
    CHECK(v4[5] == 0, "resize 新元素默认初始化");
    v4.resize(2);
    CHECK_EQ((isize)v4.size(), 2, "resize 缩小");

    /* copy 构造 + 独立性 */
    std::vector<int> v5(v4);
    CHECK_EQ((isize)v5.size(), 2, "copy 构造 size 一致");
    v5[0] = 99;
    CHECK(v4[0] != 99, "copy 后修改不影响原 vector");

    /* clear / front / back */
    v5.clear();
    CHECK(v5.empty(), "clear 后为空");
    std::vector<int> v6(2, 5);
    CHECK(v6.front() == 5 && v6.back() == 5, "front/back 正确");

    TEST_END();
}
