#pragma once
#include "Tensor.h"
#include "my/stdcompat.h"

// AdamW：自适应步长 + 解耦权重衰减
//   m_t = b1 m + (1-b1) g
//   v_t = b2 v + (1-b2) g^2
//   w  -= lr * ( m̂/(√v̂+eps) + wd * w )
// 关键：按梯度的运行 RMS 自适应缩放，解决 Cora 稀疏小特征导致的梯度过小、
// 纯 SGD 爬不动的问题。
template<typename T>
struct Adam {
    DenseMatrix<T> m, v;
    T lr, wd, b1, b2, b1t, b2t, eps;
    long t = 0;

    Adam() : lr(0), wd(0), b1(0), b2(0), eps(0), b1t(1.0), b2t(1.0) {}
    Adam(int r, int c, T lr_, T wd_ = T(0),
         T b1_ = T(0.9), T b2_ = T(0.999), T b1t_ = T(1.0), T b2t_ = T(1.0), T eps_ = T(1e-8))
        : m(r, c), v(r, c), lr(lr_), wd(wd_), b1(b1_), b2(b2_),  b1t(b1t_), b2t(b2t_), eps(eps_) {}

    void step(DenseMatrix<T>& w, const DenseMatrix<T>& g) {
        ++t;
        b1t *= b1;   // b1^t
        b2t *= b2;   // b2^t
        T bc1 = T(1) - b1t;
        T bc2 = T(1) - b2t;
        for (size_t i = 0; i < w.data.size(); ++i) {
            T gi = g.data[i];
            m.data[i] = b1 * m.data[i] + (T(1) - b1) * gi;
            v.data[i] = b2 * v.data[i] + (T(1) - b2) * gi * gi;
            T mhat = m.data[i] / bc1;
            T vhat = v.data[i] / bc2;
            w.data[i] -= lr * (mhat / (std::sqrt(vhat) + eps) + wd * w.data[i]);
        }
    }
};

