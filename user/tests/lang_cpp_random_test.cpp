// C++ 桥接:mymath::RNG(线性同余)可复现性与范围
#include "test.h"
#include "my/stdcompat.h"

extern "C" int main() {
    TEST_START("lang_cpp_random");

    /* 同种子序列可复现 */
    mymath::RNG r1(42), r2(42);
    CHECK(r1.next() == r2.next(), "同种子序列可复现(第 1 个)");
    CHECK(r1.next() == r2.next(), "同种子序列可复现(第 2 个)");

    /* 不同种子序列不同 */
    mymath::RNG r3(1), r4(2);
    CHECK(r3.next() != r4.next(), "不同种子产生不同序列");

    /* uniform01 在 [0,1) */
    mymath::RNG r5(7);
    double u = r5.uniform01();
    CHECK(u >= 0.0 && u < 1.0, "uniform01 在 [0,1) 范围");

    /* uniform_int 在 [a,b] 闭区间 */
    int ri = r5.uniform_int(1, 6);
    CHECK(ri >= 1 && ri <= 6, "uniform_int 在 [1,6] 范围");

    /* uniform(a,b) 在 [a,b] */
    double ru = r5.uniform(10.0, 20.0);
    CHECK(ru >= 10.0 && ru <= 20.0, "uniform 在 [10,20] 范围");

    /* seed 重置后可复现 */
    mymath::RNG r6(100);
    uint64_t a = r6.next();
    r6.seed(100);
    CHECK(r6.next() == a, "seed 重置后序列可复现");

    TEST_END();
}
