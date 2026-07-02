#include "Matrix.h"  
#include "Tensor.h"  
#include "my/stdcompat.h"

using Clock = std::chrono::high_resolution_clock;

static std::mt19937 gen(7);
static std::uniform_real_distribution<float> ud(0.f, 1.f);
static std::uniform_real_distribution<float> rv(-1.f, 1.f);

template<class F>
double time_ms(F&& f, int reps) {
    auto t0 = Clock::now();
    for (int r = 0; r < reps; ++r) f();
    auto t1 = Clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count() / reps;
}

// 新：随机 CSR（行内列升序）
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

// 旧：从新 CSR 构造等价的旧 Matrix（稀疏）
Matrix<float> to_old_sparse(const CSR<float>& A) {
    CSRMatrix<float> c;
    c.rows = A.rows; c.cols = A.cols;
    c.row_ptr = A.row_ptr; c.col_idx = A.col_idx; c.values = A.values;
    return Matrix<float>::fromCSR(c);
}
// 旧：从新 Dense 构造等价的旧 Matrix（稠密）
Matrix<float> to_old_dense(const DenseMatrix<float>& B) {
    Matrix<float> M(B.rows, B.cols);
    M.mode = Matrix<float>::Mode::DENSE;
    M.dense_data.assign(B.rows, std::vector<float>(B.cols));
    for (int i = 0; i < B.rows; ++i)
        for (int j = 0; j < B.cols; ++j)
            M.dense_data[i][j] = B(i, j);
    M.nnz = B.rows * B.cols;
    return M;
}

int main() {
    const int F = 64;  // 稠密特征宽度

    printf("=== old Matrix SpMM  vs  new CSR::spmm  (Â: n×n, 稀疏; X: n×%d, 稠密) ===\n", F);
    printf("%6s %8s | %12s %12s %10s\n", "n", "density", "old(ms)", "new(ms)", "speedup");
    for (int n : {64, 128, 256}) {
        double d = 0.05;
        CSR<float> A = make_csr(n, d);
        DenseMatrix<float> B = make_dense(n, F);
        Matrix<float> Ao = to_old_sparse(A), Bo = to_old_dense(B);

        double t_old = time_ms([&]{ volatile auto C = Ao * Bo; (void)C; }, 1);
        double t_new = time_ms([&]{ volatile auto C = A.spmm(B); (void)C; }, 20);
        printf("%6d %8.2f | %12.3f %12.4f %9.0fx\n", n, d, t_old, t_new, t_old / t_new);
    }

    printf("\n=== new CSR::spmm  vs  dense GEMM (Â 当稠密)  在更大规模上 ===\n");
    printf("%6s %8s | %12s %12s %10s\n", "n", "density", "spmm(ms)", "dense(ms)", "speedup");
    for (int n : {512, 1024, 2048}) {
        for (double d : {0.01, 0.05}) {
            CSR<float> A = make_csr(n, d);
            DenseMatrix<float> B = make_dense(n, F);
            DenseMatrix<float> Ad = A.to_dense();

            int reps = n <= 1024 ? 10 : 3;
            double t_spmm  = time_ms([&]{ volatile auto C = A.spmm(B);     (void)C; }, reps);
            double t_dense = time_ms([&]{ volatile auto C = matmul(Ad, B); (void)C; }, reps);
            printf("%6d %8.2f | %12.4f %12.4f %9.1fx\n", n, d, t_spmm, t_dense, t_dense / t_spmm);
        }
    }
    return 0;
}
