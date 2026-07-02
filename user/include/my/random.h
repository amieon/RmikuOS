#pragma once
#include "compat.h"
#include "cmath.h"

namespace mymath {

class RNG {
    uint64_t state_;
public:
    explicit RNG(uint64_t seed = 42) : state_(seed) {}

    uint64_t next() {
        state_ = state_ * 6364136223846793005ULL + 1;
        return state_;
    }
    
    void seed(uint64_t s) { state_ = s; }

    double uniform01() {
        return (next() >> 11) * (1.0 / (1ULL << 53));
    }

    double uniform(double a, double b) {
        return a + uniform01() * (b - a);
    }

    int uniform_int(int a, int b) {
        return a + (int)(uniform01() * (b - a + 1));
    }

    bool bernoulli(double p) {
        return uniform01() < p;
    }

    double normal(double mean = 0.0, double std = 1.0) {
        double u1 = uniform01();
        double u2 = uniform01();
        double r = sqrt(-2.0 * log(u1));
        double theta = 2.0 * 3.14159265358979323846 * u2;
        return mean + std * r * cos(theta);
    }
};

inline RNG& global_rng() {
    static RNG g(42);
    return g;
}

inline void seed_rng(unsigned s) {
    global_rng() = RNG(s);
}

// 分布类（兼容原 std:: 接口）
template <typename T>
struct uniform_real_distribution {
    T a, b;
    uniform_real_distribution(T _a = T(0), T _b = T(1)) : a(_a), b(_b) {}
    T operator()(RNG& rng) { return (T)(rng.uniform01() * (b - a) + a); }
};

template <typename T>
struct normal_distribution {
    T mean, stddev;
    normal_distribution(T m = T(0), T s = T(1)) : mean(m), stddev(s) {}
    T operator()(RNG& rng) { return (T)rng.normal((double)mean, (double)stddev); }
};

template <typename T>
struct uniform_int_distribution {
    int a, b;
    uniform_int_distribution(int _a = 0, int _b = 1) : a(_a), b(_b) {}
    int operator()(RNG& rng) { return rng.uniform_int(a, b); }
};

// Fisher-Yates shuffle
template <typename Iter>
void shuffle(Iter first, Iter last, RNG& rng) {
    size_t n = last - first;
    if (n < 2) return;
    for (size_t i = n - 1; i > 0; --i) {
        size_t j = (size_t)rng.uniform_int(0, (int)i);
        mv::swap(first[i], first[j]);
    }
}

} // namespace mymath