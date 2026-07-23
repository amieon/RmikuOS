
#include "my/stdcompat.h"

extern "C" int main() {
    int errors = 0;

    // RNG 可复现性
    mymath::RNG rng1(42), rng2(42);
    if (rng1.next() != rng2.next()) { uprintf("FAIL: RNG reproducibility\n"); errors++; }

    // uniform01 范围 [0,1)
    bool in_range = true;
    for (int i = 0; i < 100; i++) {
        double u = rng1.uniform01();
        if (u < 0 || u >= 1) in_range = false;
    }
    if (!in_range) { uprintf("FAIL: uniform01 range\n"); errors++; }

    // uniform(a,b)
    double u2 = rng1.uniform(5.0, 10.0);
    if (u2 < 5.0 || u2 >= 10.0) { uprintf("FAIL: uniform range\n"); errors++; }

    // bernoulli
    int heads = 0;
    for (int i = 0; i < 1000; i++) if (rng1.bernoulli(0.5)) heads++;
    if (heads < 400 || heads > 600) { uprintf("WARN: bernoulli skewed (%d/1000)\n", heads); }

    // normal 均值
    mymath::RNG rng3(123);
    double sum = 0; for (int i = 0; i < 1000; i++) sum += rng3.normal(10.0, 2.0);
    double mean = sum / 1000;
    if (mean < 9.0 || mean > 11.0) { uprintf("FAIL: normal mean (got %f)\n", mean); errors++; }

    // shuffle
    int arr[] = {1,2,3,4,5,6,7,8,9,10};
    int orig_sum = 0; for (int i = 0; i < 10; i++) orig_sum += arr[i];
    mymath::shuffle(arr, arr + 10, rng1);
    int shuf_sum = 0; for (int i = 0; i < 10; i++) shuf_sum += arr[i];
    if (orig_sum != shuf_sum) { uprintf("FAIL: shuffle lost elements\n"); errors++; }
    bool changed = false;
    for (int i = 0; i < 10; i++) if (arr[i] != i+1) changed = true;
    if (!changed) { uprintf("WARN: shuffle didn't change order\n"); }

    // 分布类
    std::uniform_real_distribution<double> ud(0.0, 1.0);
    double ud_val = ud(rng1);
    if (ud_val < 0 || ud_val >= 1) { uprintf("FAIL: uniform_real_distribution\n"); errors++; }

    std::normal_distribution<double> nd(0.0, 1.0);
    double nd_val = nd(rng1);
    if (nd_val < -5 || nd_val > 5) { uprintf("FAIL: normal_distribution\n"); errors++; }

    uprintf("random: %d errors\n", errors);
    return errors;
}