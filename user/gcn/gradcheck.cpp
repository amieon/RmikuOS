#include "GATLayer.h"
#include "Func.h"
#include <cstdio>
#include <cmath>
#include <algorithm>

// 用 double + 中心差分验证反向传播：解析梯度 vs 数值梯度
using T = double;

int main() {
    seed_rng(2);
    const int n = 5, F_in = 3, F_hidden = 4, F_out = 2;

    auto edges = random_symmetric_edges(n, 0.5);
    CSR<T> Ahat = build_normalized_adj<T>(n, edges);

    DenseMatrix<T> X(n, F_in), Y(n, F_out);
    init_gaussian(X); init_gaussian(Y);

    DenseMatrix<T> W1(F_in, F_hidden), W2(F_hidden, F_out);
    init_xavier(W1); init_xavier(W2);

    GATLayer<T> L1(W1), L2(W2);
    L1.set_adj(Ahat); L2.set_adj(Ahat);

    auto loss_fn = [&]() {
        DenseMatrix<T> h = L1.forward(X);
        DenseMatrix<T> out = L2.forward(h);
        return MSELoss(out, Y);
    };

    // 解析梯度
    loss_fn();
    DenseMatrix<T> h = L1.outputH, out = L2.outputH;
    DenseMatrix<T> dOut = LossGrad(out, Y);
    L1.backward(L2.backward(dOut));
    DenseMatrix<T> gW1 = L1.gradWeight, gW2 = L2.gradWeight;

    DenseMatrix<T> gAS1 = L1.dalpha_src, gAS2 = L2.dalpha_src;
    DenseMatrix<T> gAD1 = L1.dalpha_dst, gAD2 = L2.dalpha_dst;

    // 数值梯度（中心差分）
    const T eps = 1e-4;
    auto numeric_grad = [&](DenseMatrix<T>& W) {
        DenseMatrix<T> g(W.rows, W.cols);
        for (size_t k = 0; k < W.data.size(); ++k) {
            T orig = W.data[k];
            W.data[k] = orig + eps; T lp = loss_fn();
            W.data[k] = orig - eps; T lm = loss_fn();
            W.data[k] = orig;
            g.data[k] = (lp - lm) / (2 * eps);
        }
        return g;
    };

    DenseMatrix<T> nW1 = numeric_grad(L1.weight);
    DenseMatrix<T> nW2 = numeric_grad(L2.weight);

    DenseMatrix<T> nAS1 = numeric_grad(L1.alpha_src);
    DenseMatrix<T> nAS2 = numeric_grad(L2.alpha_src);

    DenseMatrix<T> nAD1 = numeric_grad(L1.alpha_dst);
    DenseMatrix<T> nAD2 = numeric_grad(L2.alpha_dst);


    auto max_rel_err = [](const DenseMatrix<T>& a, const DenseMatrix<T>& b) {
        T m = 0;
        for (size_t k = 0; k < a.data.size(); ++k) {
            T num = std::fabs(a.data[k] - b.data[k]);
            T den = std::max((T)1e-12, std::max(std::fabs(a.data[k]), std::fabs(b.data[k])));
            m = std::max(m, num / den);
        }
        return m;
    };


    auto max_abs_err = [](const DenseMatrix<T>& a, const DenseMatrix<T>& b) {
        T m = 0;
        for (size_t k = 0; k < a.data.size(); ++k) {
            T diff = std::fabs(a.data[k] - b.data[k]);
            if (diff > m) m = diff;
        }
        return m;
    };


    double relW1 = max_rel_err(gW1, nW1);
    double relW2 = max_rel_err(gW2, nW2);
    double relAS1 = max_rel_err(gAS1, nAS1);
    double relAS2 = max_rel_err(gAS2, nAS2);
    double relAD1 = max_rel_err(gAD1, nAD1);
    double relAD2 = max_rel_err(gAD2, nAD2);

    double absW1 = max_abs_err(gW1, nW1);
    double absW2 = max_abs_err(gW2, nW2);
    double absAS1 = max_abs_err(gAS1, nAS1);
    double absAS2 = max_abs_err(gAS2, nAS2);
    double absAD1 = max_abs_err(gAD1, nAD1);
    double absAD2 = max_abs_err(gAD2, nAD2);


    printf("gradient check (analytic vs numeric, central diff, double)\n");
    printf("  W1  relative error = %.3e, absolute error = %.3e\n", relW1, absW1);
    printf("  W2  relative error = %.3e, absolute error = %.3e\n", relW2, absW2);
    printf("  AS1 relative error = %.3e, absolute error = %.3e\n", relAS1, absAS1);
    printf("  AS2 relative error = %.3e, absolute error = %.3e\n", relAS2, absAS2);
    printf("  AD1 relative error = %.3e, absolute error = %.3e\n", relAD1, absAD1);
    printf("  AD2 relative error = %.3e, absolute error = %.3e\n", relAD2, absAD2);

    // ---------- 最终判决（依然只看相对误差）----------
    bool all_pass = (relW1  < 1e-5) &&
                    (relW2  < 1e-5) &&
                    (relAS1 < 1e-5) &&
                    (relAS2 < 1e-5) &&
                    (relAD1 < 1e-5) &&
                    (relAD2 < 1e-5);

    printf("  -> %s\n", all_pass ? "PASS (backward is correct)" : "FAIL");

    printf("  -> %s\n", all_pass ? "PASS (backward is correct)" : "FAIL");
    return 0;
}
