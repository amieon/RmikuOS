#pragma once
#include "my/stdcompat.h"



template<typename T>
struct DenseMatrix {
    int rows = 0, cols = 0;
    std::vector<T> data;  // row-major, 连续存储

    DenseMatrix() = default;
    DenseMatrix(int r, int c) : rows(r), cols(c), data((size_t)r * c, T{}) {}

    inline T&       operator()(int i, int j)       { return data[(size_t)i * cols + j]; }
    inline const T& operator()(int i, int j) const { return data[(size_t)i * cols + j]; }

    inline T*       row_ptr(int i)       { return &data[(size_t)i * cols]; }
    inline const T* row_ptr(int i) const { return &data[(size_t)i * cols]; }
};


template<typename T>
DenseMatrix<T>& operator+=(DenseMatrix<T>& lhs, const DenseMatrix<T>& rhs) {
    assert (!(lhs.rows != rhs.rows || lhs.cols != rhs.cols)) ;
    T* ldata = lhs.data.data();
    const T* rdata = rhs.data.data();
    const size_t n = lhs.data.size();  // 总元素个数
    for (size_t i = 0; i < n; ++i) {
        ldata[i] += rdata[i];
    }
    return lhs;

}


template<typename T>
DenseMatrix<T> operator+(const DenseMatrix<T>& lhs, const DenseMatrix<T>& rhs) {
    DenseMatrix<T> result = lhs;   // 拷贝构造
    result += rhs;                 // 复用 +=
    return result;                 // NRVO/移动语义优化
}

template<typename T>
void AVX_mul(T* crow, T a, const T*brow, int N){
#ifdef USE_AVX
    if constexpr (std::is_same_v<T, float>){
        int j = 0;
        __m256 scale = _mm256_set1_ps(a);
        for(; j <= N - 8;j += 8){
            __m256 vec = _mm256_loadu_ps(brow + j);
            __m256 result = _mm256_mul_ps(vec, scale);

            __m256 original = _mm256_loadu_ps(crow + j);
            __m256 new_val = _mm256_add_ps(original, result);
            _mm256_storeu_ps(crow + j, new_val);
        }
        for (; j < N; ++j)
            crow[j] += a * brow[j];
    }
    else if constexpr (std::is_same_v<T, double>) {
        int j = 0;
        __m256d scale = _mm256_set1_pd(a);
        for(; j <= N - 4;j += 4){
            __m256d vec = _mm256_loadu_pd(brow + j);
            __m256d result = _mm256_mul_pd(vec, scale);

            __m256d original = _mm256_loadu_pd(crow + j);
            __m256d new_val = _mm256_add_pd(original, result);
            _mm256_storeu_pd(crow + j, new_val);
        }
        for (; j < N; ++j)
            crow[j] += a * brow[j];
    }
    else {
        for (int j = 0; j < N; ++j)
            crow[j] += a * brow[j];
    }
#else
    for (int j = 0; j < N; ++j)
        crow[j] += a * brow[j];
#endif
}
// C = A * B   (ikj 顺序：对 B 的行做连续 AXPY，cache 友好)
template<typename T>
DenseMatrix<T> matmul(const DenseMatrix<T>& A, const DenseMatrix<T>& B) {
    assert(A.cols == B.rows);
    const int M = A.rows, K = A.cols, N = B.cols;
    DenseMatrix<T> C(M, N);
    for (int i = 0; i < M; ++i) {
        T* crow = C.row_ptr(i);
        const T* arow = A.row_ptr(i);
        for (int k = 0; k < K; ++k) {
            const T a = arow[k];
            const T* brow = B.row_ptr(k);
            AVX_mul(crow,a,brow,N);
        }
    }
    return C;
}

// C = A^T * B   (A: M×P, 结果 P×N；用于 dW = X^T dM)
template<typename T>
DenseMatrix<T> matmul_AtB(const DenseMatrix<T>& A, const DenseMatrix<T>& B) {
    assert(A.rows == B.rows);
    const int M = A.rows, P = A.cols, N = B.cols;
    DenseMatrix<T> C(P, N);
    for (int k = 0; k < M; ++k) {
        const T* arow = A.row_ptr(k);   // A[k][*]
        const T* brow = B.row_ptr(k);   // B[k][*]
        for (int i = 0; i < P; ++i) {
            const T a = arow[i];
            T* crow = C.row_ptr(i);
            AVX_mul(crow,a,brow,N);
        }
    }
    return C;
}

// C = A * B^T   (A: M×K, B: N×K, 结果 M×N；用于 dX = dM W^T)
template<typename T>
DenseMatrix<T> matmul_ABt(const DenseMatrix<T>& A, const DenseMatrix<T>& B) {
    assert(A.cols == B.cols);
    const int M = A.rows, K = A.cols, N = B.rows;
    DenseMatrix<T> C(M, N);
    for (int i = 0; i < M; ++i) {
        const T* arow = A.row_ptr(i);
        T* crow = C.row_ptr(i);
        for (int j = 0; j < N; ++j) {
            const T* brow = B.row_ptr(j);
            T s = T{};
#ifdef USE_AVX
            if constexpr (std::is_same_v<T, float>){
                __m256 sum_vec = _mm256_setzero_ps();
                int l = 0;
                for(; l <= K - 8;l += 8){
                    __m256 vec_a = _mm256_loadu_ps(arow + l);
                    __m256 vec_b = _mm256_loadu_ps(brow + l);
                    __m256 result = _mm256_mul_ps(vec_a, vec_b);
                    sum_vec = _mm256_add_ps(sum_vec, result);
                }
                T sum[8];
                _mm256_storeu_ps(sum, sum_vec);
                for (int p = 0; p < 8; ++p) s += sum[p];
                for (; l < K; ++l)
                    s += arow[l] * brow[l];
                crow[j] = s;
            }
            else if constexpr (std::is_same_v<T, double>) {
                __m256d sum_vec = _mm256_setzero_pd();
                int l = 0;
                for(; l <= K - 4;l += 4){
                    __m256d vec_a = _mm256_loadu_pd(arow + l);
                    __m256d vec_b = _mm256_loadu_pd(brow + l);
                    __m256d result = _mm256_mul_pd(vec_a, vec_b);
                    sum_vec = _mm256_add_pd(sum_vec, result);

                }
                T sum[8];
                _mm256_storeu_pd(sum, sum_vec);
                for (int p = 0; p < 4; ++p) s += sum[p];
                for (; l < K; ++l)
                    s += arow[l] * brow[l];
                crow[j] = s;
            }
            else {
                for (int l = 0; l < K; ++l)
                    s += arow[l] * brow[l];
                crow[j] = s;
            }
#else
            for (int l = 0; l < K; ++l)
                s += arow[l] * brow[l];
            crow[j] = s;
#endif


        }
    }
    return C;
}

// 逐元素 Hadamard 积
template<typename T>
DenseMatrix<T> hadamard(const DenseMatrix<T>& A, const DenseMatrix<T>& B) {
    assert(A.rows == B.rows && A.cols == B.cols);
    DenseMatrix<T> C(A.rows, A.cols);
    for (size_t i = 0; i < A.data.size(); ++i)
        C.data[i] = A.data[i] * B.data[i];
    return C;
}



// A <- A - lr * G   (原地更新，避免重新分配)
template<typename T>
void axpy_update(DenseMatrix<T>& A, const DenseMatrix<T>& G, T lr) {
    assert(A.rows == G.rows && A.cols == G.cols);
    for (size_t i = 0; i < A.data.size(); ++i)
        A.data[i] -= lr * G.data[i];
}

// ---------------------------------------------------------------------------
// CSR 稀疏矩阵：真正的压缩稀疏行
// ---------------------------------------------------------------------------
template<typename T>
struct CSR {
    int rows = 0, cols = 0;
    std::vector<int> row_ptr;   // 长度 rows+1
    std::vector<int> col_idx;   // 长度 nnz
    std::vector<T>   values;    // 长度 nnz

    CSR() = default;
    CSR(int r, int c) : rows(r), cols(c), row_ptr(r + 1, 0) {}

    int nnz() const { return (int)values.size(); }
    double density() const { return rows && cols ? (double)nnz() / ((double)rows * cols) : 0.0; }

    // 核心算子：C = this(稀疏 m×k) * B(稠密 k×n) -> C(稠密 m×n)
    // 每个非零做一次对稠密行的 AXPY，全程顺序访存，无 set/get 开销。
    DenseMatrix<T> spmm(const DenseMatrix<T>& B) const {
        assert(cols == B.rows);
        const int N = B.cols;
        DenseMatrix<T> C(rows, N);
        // 行级并行：每行独占写 C 的一行，无数据竞争。
        // 未启用 -fopenmp 时此 pragma 被忽略，退化为串行。
        #pragma omp parallel for schedule(static)
        for (int i = 0; i < rows; ++i) {
            T* crow = C.row_ptr(i);
            for (int idx = row_ptr[i]; idx < row_ptr[i + 1]; ++idx) {
                const T a = values[idx];
                const T* brow = B.row_ptr(col_idx[idx]);
                AVX_mul(crow,a,brow,N);
            }
        }
        return C;
    }

    // 转置：构造 A^T 的 CSR（用于反向传播；对称图下 A^T == A，但保持通用正确）
    CSR<T> transpose() const {
        CSR<T> Tm(cols, rows);
        Tm.row_ptr.assign(cols + 1, 0);
        for (int idx = 0; idx < nnz(); ++idx)
            Tm.row_ptr[col_idx[idx] + 1]++;
        for (int i = 0; i < cols; ++i)
            Tm.row_ptr[i + 1] += Tm.row_ptr[i];
        Tm.col_idx.resize(nnz());
        Tm.values.resize(nnz());
        std::vector<int> cursor(Tm.row_ptr.begin(), Tm.row_ptr.end() - 1);
        for (int i = 0; i < rows; ++i)
            for (int idx = row_ptr[i]; idx < row_ptr[i + 1]; ++idx) {
                int c = col_idx[idx];
                int pos = cursor[c]++;
                Tm.col_idx[pos] = i;
                Tm.values[pos] = values[idx];
            }
        return Tm;
    }

    // 转成稠密（仅供 benchmark 对照用）
    DenseMatrix<T> to_dense() const {
        DenseMatrix<T> D(rows, cols);
        for (int i = 0; i < rows; ++i)
            for (int idx = row_ptr[i]; idx < row_ptr[i + 1]; ++idx)
                D(i, col_idx[idx]) = values[idx];
        return D;
    }

    // 从已排序的 (i, j, val) 三元组构造（同一行内 j 递增）
    static CSR<T> from_sorted_triplets(int r, int c,
                                       const std::vector<std::tuple<int,int,T>>& t) {
        CSR<T> M(r, c);
        M.col_idx.reserve(t.size());
        M.values.reserve(t.size());
        int cur = 0;
        for (auto& [i, j, v] : t) {
            while (cur < i) M.row_ptr[++cur] = (int)M.values.size();
            M.col_idx.push_back(j);
            M.values.push_back(v);
        }
        while (cur < r) M.row_ptr[++cur] = (int)M.values.size();
        return M;
    }
};
