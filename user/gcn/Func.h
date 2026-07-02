#pragma once
#include "Tensor.h"
#include "my/stdcompat.h"

// 全局可复现 RNG
inline std::mt19937& global_rng() { static std::mt19937 gen(42); return gen; }
inline void seed_rng(unsigned s) { global_rng().seed(s); }

// ---- 激活 / 损失（稠密）----
template<typename T>
DenseMatrix<T> relu(const DenseMatrix<T>& m) {
    DenseMatrix<T> out(m.rows, m.cols);
    for (size_t i = 0; i < m.data.size(); ++i)
        out.data[i] = m.data[i] > T(0) ? m.data[i] : T(0);
    return out;
}
template<typename T>
DenseMatrix<T> relu_deriv(const DenseMatrix<T>& m) {
    DenseMatrix<T> out(m.rows, m.cols);
    for (size_t i = 0; i < m.data.size(); ++i)
        out.data[i] = m.data[i] > T(0) ? T(1) : T(0);
    return out;
}
template<typename T>
T MSELoss(const DenseMatrix<T>& pred, const DenseMatrix<T>& label) {
    assert(pred.rows == label.rows && pred.cols == label.cols);
    T loss = T(0);
    for (size_t i = 0; i < pred.data.size(); ++i) {
        T d = pred.data[i] - label.data[i];
        loss += d * d;
    }
    return loss / T(pred.data.size());
}
template<typename T>
DenseMatrix<T> LossGrad(const DenseMatrix<T>& pred, const DenseMatrix<T>& label) {
    assert(pred.rows == label.rows && pred.cols == label.cols);
    DenseMatrix<T> g(pred.rows, pred.cols);
    T scale = T(2) / T(pred.data.size());  // d(MSE)/d(pred)
    for (size_t i = 0; i < pred.data.size(); ++i)
        g.data[i] = scale * (pred.data[i] - label.data[i]);
    return g;
}

// ---- 初始化 ----
template<typename T>
void init_xavier(DenseMatrix<T>& A) {
    T limit = std::sqrt(T(6) / T(A.rows + A.cols));
    std::uniform_real_distribution<T> dist(-limit, limit);
    for (auto& x : A.data) x = dist(global_rng());
}
template<typename T>
void init_gaussian(DenseMatrix<T>& A, T mean = T(0), T stddev = T(1)) {
    std::normal_distribution<T> dist(mean, stddev);
    for (auto& x : A.data) x = dist(global_rng());
}

// ---- 图：随机对称边 + 对称归一化邻接（直接构造 CSR）----
inline std::vector<std::pair<int,int>> random_symmetric_edges(int n, double p) {
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    std::vector<std::pair<int,int>> edges;
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
            if (dist(global_rng()) < p) edges.emplace_back(i, j);
    return edges;
}

// Â = D^{-1/2} (A + I) D^{-1/2}，返回 CSR（行内列升序）
template<typename T>
CSR<T> build_normalized_adj(int n, const std::vector<std::pair<int,int>>& edges) {
    std::vector<std::vector<int>> nbr(n);
    for (int i = 0; i < n; ++i) nbr[i].push_back(i);  // 自环
    for (auto& pr : edges) { nbr[pr.first].push_back(pr.second); nbr[pr.second].push_back(pr.first); }

    std::vector<T> inv_sqrt_deg(n);
    for (int i = 0; i < n; ++i) {
        std::sort(nbr[i].begin(), nbr[i].end());
        nbr[i].erase(std::unique(nbr[i].begin(), nbr[i].end()), nbr[i].end());
        inv_sqrt_deg[i] = T(1) / std::sqrt(T(nbr[i].size()));
    }

    std::vector<std::tuple<int,int,T>> trips;
    for (int i = 0; i < n; ++i)
        for (int j : nbr[i])
            trips.emplace_back(i, j, inv_sqrt_deg[i] * inv_sqrt_deg[j]);
    return CSR<T>::from_sorted_triplets(n, n, trips);
}



// 行 softmax（数值稳定：每行减去最大值）
template<typename T>
DenseMatrix<T> segment_softmax (const DenseMatrix<T>& Z) {
    DenseMatrix<T> P(Z.rows, Z.cols);
    for (int i = 0; i < Z.rows; ++i) {
        const T* z = Z.row_ptr(i);
        T* p = P.row_ptr(i);
        T mx = z[0];
        for (int j = 1; j < Z.cols; ++j) mx = std::max(mx, z[j]);
        T sum = T(0);
        for (int j = 0; j < Z.cols; ++j) { p[j] = std::exp(z[j] - mx); sum += p[j]; }
        for (int j = 0; j < Z.cols; ++j) p[j] /= sum;
    }
    return P;
}

// 行 softmax（数值稳定：每行减去最大值）
template<typename T>
CSR<T> sparse_softmax(const CSR<T>& Z) {
    CSR<T> P = Z;
    const int* row_ptr = Z.row_ptr.data();  // 原始指针避免 vector 开销
    const T* z_vals = Z.values.data();
    T* p_vals = P.values.data();

    for (int i = 0; i < Z.rows; ++i) {
        int start = row_ptr[i], end = row_ptr[i + 1];   // ← 这一段就是节点 i 的邻居
        if (start == end) continue;                      // 没有邻居，跳过

        // 1. 找这一段的最大值（数值稳定，和你 Dense 版一个道理）
        T mx = z_vals[start];
        for (int j = start; j < end; ++j) mx = std::max(mx, z_vals[j]);
        // 2. 每个 P[idx] -> exp(Z[idx] - max)，同时累加 sum
        T sum = T(0);
        for (int j = start; j < end; ++j) { p_vals[j] = std::exp(z_vals[j] - mx); sum += p_vals[j]; }
        // 3. 每个 P[idx] /= sum
        for (int j = start; j < end; ++j) p_vals[j] /= sum;
        // —— 三个循环都是 for (idx = start; idx < end; ++idx)
    }
    return P;
}

// masked 交叉熵： L = -1/|mask| Σ_{i∈mask} log P[i, y_i]
template<typename T>
T cross_entropy_masked(const DenseMatrix<T>& P, const std::vector<int>& y,
                       const std::vector<char>& mask) {
    T loss = T(0); int cnt = 0;
    for (int i = 0; i < P.rows; ++i)
        if (mask[i]) {
            loss += -std::log(std::max(P(i, y[i]), T(1e-12)));
            ++cnt;
        }
    return cnt ? loss / T(cnt) : T(0);
}

// softmax + CE 的合并梯度（对 logits）： (P - onehot)/|mask|，mask 外为 0
template<typename T>
DenseMatrix<T> softmax_ce_grad(const DenseMatrix<T>& P, const std::vector<int>& y,
                               const std::vector<char>& mask) {
    DenseMatrix<T> g(P.rows, P.cols);
    int cnt = 0;
    for (int i = 0; i < P.rows; ++i) if (mask[i]) ++cnt;
    if (!cnt) return g;
    for (int i = 0; i < P.rows; ++i)
        if (mask[i]) {
            for (int c = 0; c < P.cols; ++c) g(i, c) = P(i, c) / T(cnt);
            g(i, y[i]) -= T(1) / T(cnt);
        }
    return g;
}

// masked 准确率
template<typename T>
double accuracy_masked(const DenseMatrix<T>& logits, const std::vector<int>& y,
                       const std::vector<char>& mask) {
    int correct = 0, total = 0;
    for (int i = 0; i < logits.rows; ++i)
        if (mask[i]) {
            const T* z = logits.row_ptr(i);
            int arg = 0;
            for (int c = 1; c < logits.cols; ++c) if (z[c] > z[arg]) arg = c;
            correct += (arg == y[i]); ++total;
        }
    return total ? double(correct) / total : 0.0;
}


template<typename T>
struct Dropout {
    T p;
    DenseMatrix<T> mask;  // 存 mask*scale（0 或 1/(1-p)）

    explicit Dropout(T p_ = T(0)) : p(p_) {}

    DenseMatrix<T> forward(const DenseMatrix<T>& X, bool training) {
        if (!training || p <= T(0)) { mask = DenseMatrix<T>(); return X; }
        T scale = T(1) / (T(1) - p);
        mask = DenseMatrix<T>(X.rows, X.cols);
        DenseMatrix<T> out(X.rows, X.cols);
        std::uniform_real_distribution<T> u(T(0), T(1));
        for (size_t i = 0; i < X.data.size(); ++i) {
            T keep = (u(global_rng()) < p) ? T(0) : scale;
            mask.data[i] = keep;
            out.data[i] = X.data[i] * keep;
        }
        return out;
    }

    DenseMatrix<T> backward(const DenseMatrix<T>& dY) {
        if (mask.data.empty()) return dY;  // 未应用（推理或 p=0）
        DenseMatrix<T> dX(dY.rows, dY.cols);
        for (size_t i = 0; i < dY.data.size(); ++i)
            dX.data[i] = dY.data[i] * mask.data[i];
        return dX;
    }
};
