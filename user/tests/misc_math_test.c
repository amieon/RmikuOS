#include "test.h"
#include "math.h"

/* 基础数学库(stdio/math 已移至 include,标准名宏映射到 mm_*) */
static int close2(double a, double b) {
    double d = a - b;
    return (d < 0 ? -d : d) < 1e-9;
}

int main() {
    TEST_START("misc_math");

    CHECK(close2(sqrt(4.0), 2.0), "sqrt(4)=2");
    CHECK(close2(sqrt(2.0) * sqrt(2.0), 2.0), "sqrt(2)*sqrt(2)=2");
    CHECK(close2(fabs(-3.5), 3.5), "fabs(-3.5)=3.5");
    CHECK(close2(exp(0.0), 1.0), "exp(0)=1");
    CHECK(close2(log(1.0), 0.0), "log(1)=0");
    CHECK(close2(log2(8.0), 3.0), "log2(8)=3");
    CHECK(close2(sin(0.0), 0.0), "sin(0)=0");
    CHECK(close2(cos(0.0), 1.0), "cos(0)=1");
    CHECK(close2(pow(2.0, 10.0), 1024.0), "pow(2,10)=1024");
    CHECK(close2(floor(3.7), 3.0), "floor(3.7)=3");
    CHECK(close2(ceil(3.2), 4.0), "ceil(3.2)=4");

    /* 混合运算:sin²+cos²=1 */
    double s = sin(1.234), c = cos(1.234);
    CHECK(close2(s * s + c * c, 1.0), "sin²+cos²=1");

    TEST_END();
}
