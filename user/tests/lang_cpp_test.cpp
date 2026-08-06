// C++ 语言桥接测试:不依赖 STL,用裸 C++ 特性(类/重载/引用)
#include "test.h"

namespace {

class Counter {
public:
    Counter() : value_(0) {}
    void add(int n) { value_ += n; }
    int get() const { return value_; }
private:
    int value_;
};

int twice(int x) { return x * 2; }
double twice(double x) { return x * 2.0; }  /* 重载 */

}  // namespace

extern "C" int main() {
    TEST_START("lang_cpp");

    Counter c;
    c.add(21);
    c.add(21);
    CHECK_EQ(c.get(), 42, "C++ 类成员函数与状态正常");

    CHECK_EQ(twice(3), 6, "C++ 函数重载(int)");
    /* double 重载:2.5*2=5.0,用范围比较避免浮点边界问题 */
    double d = twice(2.5);
    CHECK(d >= 4.99 && d <= 5.01, "C++ 函数重载(double)");

    int x = 10;
    int &ref = x;
    ref = 20;
    CHECK_EQ(x, 20, "C++ 引用别名生效");

    TEST_END();
}
