#pragma once
#include "Tensor.h"
#include "Func.h"
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <numeric>
#include <algorithm>

template<typename T>
struct Graph {
    int n = 0, num_classes = 0;
    CSR<T> Ahat;
    DenseMatrix<T> X;
    std::vector<int> labels;
    std::vector<char> train_mask, val_mask, test_mask;
};

// 划分 train/val/test 掩码：每类取 per_class_train 个作训练，再各取 n_val / n_test
template<typename T>
void build_splits(Graph<T>& g, int per_class_train, int n_val, int n_test) {
    int n = g.n, C = g.num_classes;
    g.train_mask.assign(n, 0);
    g.val_mask.assign(n, 0);
    g.test_mask.assign(n, 0);

    std::vector<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    std::shuffle(order.begin(), order.end(), global_rng());

    std::vector<int> taken(C, 0);
    int n_train_total = 0;
    for (int idx : order) {
        int c = g.labels[idx];
        if (taken[c] < per_class_train) { g.train_mask[idx] = 1; taken[c]++; n_train_total++; }
    }
    int v = 0, t = 0;
    for (int idx : order) {
        if (g.train_mask[idx]) continue;
        if (v < n_val) { g.val_mask[idx] = 1; ++v; }
        else if (t < n_test) { g.test_mask[idx] = 1; ++t; }
    }
}

// L1 行归一化特征（Cora 的常规预处理：每行除以行和）
template<typename T>
void row_normalize(DenseMatrix<T>& X) {
    for (int i = 0; i < X.rows; ++i) {
        T s = T(0);
        for (int j = 0; j < X.cols; ++j) s += X(i, j);
        if (s > T(0)) for (int j = 0; j < X.cols; ++j) X(i, j) /= s;
    }
}

// ---- 标准 Cora 加载器 ----
//   content 文件每行： <paper_id> <1433 个 0/1 特征> <class_label>
//   cites   文件每行： <cited_id> <citing_id>
//   用法（本地联网下载 cora 后）：
//     auto g = load_cora<float>("cora.content", "cora.cites");
template<typename T>
Graph<T> load_cora(const std::string& content_path, const std::string& cites_path) {
    std::ifstream fc(content_path);
    if (!fc) { fprintf(stderr, "无法打开 %s\n", content_path.c_str()); return {}; }

    std::vector<std::vector<T>> feats;
    std::vector<std::string> raw_labels;
    std::unordered_map<std::string, int> id2idx;
    std::vector<std::string> tokens;
    std::string line;
    while (std::getline(fc, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string tok; tokens.clear();
        while (ss >> tok) tokens.push_back(tok);
        if (tokens.size() < 3) continue;
        int idx = (int)id2idx.size();
        id2idx[tokens.front()] = idx;
        std::vector<T> f;
        f.reserve(tokens.size() - 2);
        for (size_t j = 1; j + 1 < tokens.size(); ++j) f.push_back((T)std::stod(tokens[j]));
        feats.push_back(std::move(f));
        raw_labels.push_back(tokens.back());
    }
    int n = (int)feats.size();
    int F = n ? (int)feats[0].size() : 0;

    std::unordered_map<std::string, int> lbl2idx;
    Graph<T> g;
    g.n = n;
    g.labels.resize(n);
    for (int i = 0; i < n; ++i) {
        auto it = lbl2idx.find(raw_labels[i]);
        int c = (it == lbl2idx.end()) ? (lbl2idx[raw_labels[i]] = (int)lbl2idx.size()) : it->second;
        g.labels[i] = c;
    }
    g.num_classes = (int)lbl2idx.size();

    g.X = DenseMatrix<T>(n, F);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < F; ++j) g.X(i, j) = feats[i][j];
    row_normalize(g.X);

    std::ifstream fe(cites_path);
    std::vector<std::pair<int,int>> edges;
    std::string a, b;
    while (fe >> a >> b) {
        auto ia = id2idx.find(a), ib = id2idx.find(b);
        if (ia != id2idx.end() && ib != id2idx.end() && ia->second != ib->second)
            edges.emplace_back(ia->second, ib->second);  // 视为无向
    }
    g.Ahat = build_normalized_adj<T>(n, edges);

    build_splits(g, 20, 500, 1000);  // 与常见 Cora 设定一致
    return g;
}

// ---- 合成 SBM 数据集（沙箱断网时验证管线）----
//   C 个类，每类 npc 个节点；同类连边概率 p_in、异类 p_out；
//   每类一个随机均值向量，特征 = 类均值 + 噪声 -> 图结构对分类有真实帮助
template<typename T>
Graph<T> make_sbm(int C, int npc, int d, double p_in, double p_out, T noise) {
    Graph<T> g;
    int n = C * npc;
    g.n = n; g.num_classes = C;
    g.labels.resize(n);
    for (int i = 0; i < n; ++i) g.labels[i] = i / npc;

    DenseMatrix<T> mu(C, d);
    init_gaussian(mu, T(0), T(1));
    g.X = DenseMatrix<T>(n, d);
    std::normal_distribution<T> ndist(T(0), noise);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < d; ++j)
            g.X(i, j) = mu(g.labels[i], j) + ndist(global_rng());

    std::uniform_real_distribution<double> u(0.0, 1.0);
    std::vector<std::pair<int,int>> edges;
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j) {
            double p = (g.labels[i] == g.labels[j]) ? p_in : p_out;
            if (u(global_rng()) < p) edges.emplace_back(i, j);
        }
    g.Ahat = build_normalized_adj<T>(n, edges);

    build_splits(g, 20, n / 4, n / 2);
    return g;
}

// ---- Cora 同款 regime 的合成集（用于离线复现"稀疏小特征 → 梯度过小"）----
//   高维稀疏二值词袋特征 + 行归一化；每类有偏好词；图为 SBM 社区结构。
//   特征量级与 Cora 接近，从而能复现纯 SGD 爬不动、Adam 才收敛的现象。
template<typename T>
Graph<T> make_cora_like(int C, int npc, int vocab, int words_per_node,
                        double p_in, double p_out, double topic_bias) {
    Graph<T> g;
    int n = C * npc;
    g.n = n; g.num_classes = C;
    g.labels.resize(n);
    for (int i = 0; i < n; ++i) g.labels[i] = i / npc;

    // 每类一组偏好词（在词表上的高概率区段）
    int topic_size = std::max(1, vocab / C);
    g.X = DenseMatrix<T>(n, vocab);
    std::uniform_int_distribution<int> any_word(0, vocab - 1);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    for (int i = 0; i < n; ++i) {
        int c = g.labels[i];
        int lo = c * topic_size, hi = std::min(vocab, lo + topic_size);
        std::uniform_int_distribution<int> topic_word(lo, hi - 1);
        for (int w = 0; w < words_per_node; ++w) {
            int idx = (u(global_rng()) < topic_bias) ? topic_word(global_rng())
                                                     : any_word(global_rng());
            g.X(i, idx) = T(1);  // 二值词袋
        }
    }
    row_normalize(g.X);  // 与 Cora 一致

    std::vector<std::pair<int,int>> edges;
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j) {
            double p = (g.labels[i] == g.labels[j]) ? p_in : p_out;
            if (u(global_rng()) < p) edges.emplace_back(i, j);
        }
    g.Ahat = build_normalized_adj<T>(n, edges);

    build_splits(g, 20, n / 4, n / 2);
    return g;
}
