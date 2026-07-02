#include "Tensor.h"
#include <omp.h>
#include <chrono>
#include <random>
#include <cstdio>
#include <cmath>
#include <vector>

using Clock = std::chrono::high_resolution_clock;

static std::mt19937 gen(7);
static std::uniform_real_distribution<float> ud(0.f, 1.f), rv(-1.f, 1.f);

CSR<float> make_csr(int n, double d) {
    CSR<float> A(n, n);
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j)
            if (ud(gen) < d) { A.col_idx.push_back(j); A.values.push_back(rv(gen)); }
        A.row_ptr[i + 1] = (int)A.values.size();
    }
    return A;
}
DenseMatrix<float> make_dense(int r, int c) {
    DenseMatrix<float> B(r, c);
    for (auto& x : B.data) x = rv(gen);
    return B;
}

int main() {
    const int n = 8000, F = 128;
    const double d = 0.005;
    CSR<float> A = make_csr(n, d);
    DenseMatrix<float> B = make_dense(n, F);
    printf("SpMM: A %dx%d (nnz=%d, density=%.3f) * B %dx%d\n", n, n, A.nnz(), d, n, F);

    // 正确性：串行 vs 并行结果应逐位一致（每行由单线程顺序计算，加法顺序不变）
    omp_set_num_threads(1);
    DenseMatrix<float> Cref = A.spmm(B);
    omp_set_num_threads(4);
    DenseMatrix<float> Cpar = A.spmm(B);
    double maxdiff = 0;
    for (size_t i = 0; i < Cref.data.size(); ++i)
        maxdiff = std::max(maxdiff, (double)std::fabs(Cref.data[i] - Cpar.data[i]));
    printf("正确性检查  max|serial - parallel| = %.3e  (%s)\n\n",
           maxdiff, maxdiff == 0.0 ? "逐位一致" : "存在差异");

    // 扩展性
    printf("%8s | %10s | %8s | %10s\n", "threads", "time(ms)", "speedup", "efficiency");
    double t1 = 0;
    for (int t : {1, 2, 4, 8}) {
        omp_set_num_threads(t);
        const int reps = 30;
        auto t0 = Clock::now();
        for (int r = 0; r < reps; ++r) { volatile auto C = A.spmm(B); (void)C; }
        auto t2 = Clock::now();
        double ms = std::chrono::duration<double, std::milli>(t2 - t0).count() / reps;
        if (t == 1) t1 = ms;
        printf("%8d | %10.4f | %7.2fx | %9.0f%%\n", t, ms, t1 / ms, 100.0 * (t1 / ms) / t);
    }
    return 0;
}
