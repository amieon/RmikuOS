# VeryEasyGCN — 从零实现的稀疏图神经网络（C++）

用纯 C++（不依赖任何深度学习框架）从零实现 **GCN** 与 **GAT** 两种图神经网络。项目的重点不在"实现一个模型"——而在把它当**系统问题**来做：自己写稀疏算子（SpMM / SDDMM）、做三个层次的性能优化（算法 / SIMD / 多核），并用数值梯度检验证明每一处反向传播的正确性。

核心亮点：

- **两大稀疏算子 SpMM + SDDMM**：从零实现消息传递 GNN 的两个对偶核心算子。SpMM（稀疏×稠密，输出在节点）做邻居聚合；SDDMM（稠密×稠密但只在稀疏位置取样，输出在边）算注意力分数。
- **真正高效的 CSR-SpMM kernel**：按非零做顺序访存的 AXPY，比"把稀疏矩阵当稠密做 GEMM"在稀疏图上快约 **80×**（开销随非零数 nnz 增长，而非 n²）。
- **三层性能优化**：算法层（CSR）+ 指令级（AVX2 SIMD，单线程 **3.37×**）+ 线程级（OpenMP），端到端 **~9.5×**，并分析了 memory-bound kernel 的扩展性上限。
- **GAT 图注意力层**：复用 CSR/SpMM 基础设施实现注意力机制，含完整反向传播（SDDMM 求 dα、segment softmax 反向、梯度散射、三路汇合），六个参数梯度全部通过数值检验。
- **数值梯度检验**：解析梯度 vs 中心差分，GCN/GAT 全部参数误差 1e-8 ~ 1e-11；并诊断排除了一处由"接近零的梯度分量"导致的假 FAIL。
- **完整训练栈**：softmax + 交叉熵、masked 训练/验证/测试、AdamW、Dropout，在真实 Cora 上 GCN 达 78.5%、GAT 76.1%。

---

## 项目结构

```
.
 ├── Tensor.h        # 核心：DenseMatrix（扁平 row-major）+ CSR（SpMM kernel + AVX2）
 ├── Func.h          # 激活/损失、softmax、交叉熵、初始化、Dropout、图归一化
 ├── GCNLayer.h      # GCN 层：前向 / 反向 / 参数更新（可选 ReLU / Identity 激活）
 ├── GATLayer.h      # GAT 层：注意力打分(SDDMM) + segment softmax + 完整反向
 ├── Optim.h         # AdamW 优化器（per-parameter）
 ├── Dataset.h       # Cora 加载器 + 合成数据集（SBM / Cora-like）
 ├── main.cpp        # teacher–student 训练（验证训练链路）
 ├── train_cora.cpp  # Cora 节点分类主程序（GCN）
 ├── train_cora_GTA.cpp  # Cora 节点分类主程序（GAT）
 ├── gradcheck.cpp   # 数值梯度检验（GCN + GAT 全部参数）
 ├── bench_spmm.cpp  # SpMM 性能基准（新 vs 旧 vs 稠密）
 ├── bench_omp.cpp   # OpenMP 多核扩展性基准
 └── Matrix.h        # 旧实现（已退役，仅 bench_spmm 留作对照基线）
```



---
## 架构设计

GNN 的数据流里，归一化邻接矩阵 Â 恒为稀疏、节点特征 X 与权重 W 恒为稠密——这是编译期就确定的事实。因此项目把存储拆成两个静态类型，而非用一个会在运行时按密度来回切换的矩阵类：

- **`DenseMatrix<T>`**：单块连续 `std::vector<T>`，row-major。所有稠密 GEMM（`matmul` / `matmul_AtB` / `matmul_ABt`）走裸指针、按行连续访存。
- **`CSR<T>`**：标准压缩稀疏行（`row_ptr` / `col_idx` / `values`），核心方法是 `spmm`。

### 核心算子：CSR-SpMM

`C = Â · B`（Â 稀疏 m×k，B 稠密 k×n）的实现是教科书写法——遍历每个非零，对 B 的对应稠密行做一次 AXPY 累加到 C 的行上，全程顺序访存：

```cpp
for (int i = 0; i < rows; ++i) {
    T* crow = C.row_ptr(i);
    for (int idx = row_ptr[i]; idx < row_ptr[i+1]; ++idx) {
        const T a = values[idx];
        const T* brow = B.row_ptr(col_idx[idx]);
        for (int j = 0; j < N; ++j)
            crow[j] += a * brow[j];        // AXPY
    }
}
```

每行只写自己的 C 行、互不依赖，所以最外层行循环可直接用 `#pragma omp parallel for` 并行，无数据竞争。

------

## 数学推导

### GCN 前向传播

每一层：

$$ H^{(l+1)} = \sigma!\left(\tilde{A}, H^{(l)} W^{(l)}\right),\qquad \tilde{A} = \hat{D}^{-\frac12}(A+I)\hat{D}^{-\frac12} $$

其中 $H^{(0)}=X$，$\sigma$ 为 ReLU（末层取恒等以输出 logits）。实现上拆成两步：先稠密 GEMM 算 $XW$，再用 SpMM 左乘稀疏 $\tilde{A}$。

### GCN 反向传播

设 $Z = \tilde{A}(XW)$，$M = XW$。给定上层回传的 $\partial \mathcal{L}/\partial H$：

$$ \delta_Z = \frac{\partial \mathcal{L}}{\partial H}\odot \sigma'(Z) $$

$$ \frac{\partial \mathcal{L}}{\partial M} = \tilde{A}^{\top}\delta_Z ;(\text{SpMM}),\qquad \frac{\partial \mathcal{L}}{\partial W} = X^{\top}!\left(\tilde{A}^{\top}\delta_Z\right),\qquad \frac{\partial \mathcal{L}}{\partial X} = \left(\tilde{A}^{\top}\delta_Z\right) W^{\top} $$

（对称归一化下 $\tilde{A}^{\top}=\tilde{A}$，代码仍构造转置以保持对一般图的正确性。）

### softmax + 交叉熵的合并梯度

末层输出 logits，经行 softmax 得 $P$，masked 交叉熵对 logits 的梯度有简洁形式：

$$ \frac{\partial \mathcal{L}}{\partial Z_{\text{logits}}} = \frac{1}{|\text{mask}|},(P - \text{onehot}(y)),\quad \text{仅训练节点非零} $$

------

## GAT：图注意力网络

GCN 的聚合权重 $\tilde{A}$ 是写死的（只看度数）；GAT 让模型从节点特征学出每个邻居的重要性 $\alpha_{ij}$。

**前向**：

1. 线性变换 $z_i = W h_i$
2. 注意力分数 $ e_{ij} = \text{LeakyReLU}(\vec{a}*{\text{src}}^{\top} z_i + \vec{a}*{\text{dst}}^{\top} z_j)$ —— 利用 $\vec{a}=[\vec{a}*{\text{src}},\Vert,\vec{a}*{\text{dst}}] $ 的**线性可加性**，把拼接内积拆成两个 $O(nF')$ 的矩阵-向量乘 + 逐边相加，避免 $O(n^2)$ 枚举与显式拼接
3. **segment softmax**：在每个节点的邻居集合内归一化（按 CSR 行分段）
4. 聚合 $h_i' = \sigma!\left(\sum_{j \in N(i)} \alpha_{ij} z_j\right)$ —— 即以 $\alpha$ 为值的 SpMM

**反向**完整实现：SDDMM 在边上算 $d\alpha_{ij}=\langle dZ_{\text{out}}[i],, z_j\rangle$（避免物化 $n\times n$）→ segment softmax 反向 → LeakyReLU → 梯度散射回 src/dst（行求和 / 借转置做列求和）→ $z$ 的**三路梯度汇合**（聚合链 + src 链 + dst 链）。

### SpMM 与 SDDMM：GNN 的两大稀疏算子

实现 GAT 时识别出消息传递 GNN 的两个对偶核心算子：

| 算子      | 计算                                        | 输出位置       |
| --------- | ------------------------------------------- | -------------- |
| **SpMM**  | 稀疏 × 稠密（聚合，用 α 当权重）            | 稠密（节点上） |
| **SDDMM** | 稠密 × 稠密，但只在稀疏位置取样（算边分数） | 稀疏（边上）   |

GAT = SpMM + SDDMM 交替。注意 **SpGEMM（稀疏×稀疏）在此用不到**——稀疏量（α、e）始终与稠密量（z）交互，从不出现两个稀疏矩阵相乘。识别"真正需要的是 SDDMM 而非 SpGEMM"本身就是关键的工程判断。

------

## 性能优化：三个层次

SpMM / GEMM 在三个层次上做了优化，并用 benchmark 量化每一层的贡献。

### 1. 算法层 — CSR 稀疏

只遍历非零，开销 O(nnz) 而非 O(n²)。

**新 CSR-SpMM vs 旧实现的 set/get 累加**（单线程，`-O2`，Â 为 n×n 稀疏，X 为 n×64 稠密）：

| n    | density | 旧 (ms) | 新 (ms) | 加速 |
| ---- | ------- | ------- | ------- | ---- |
| 64   | 0.05    | 0.157   | 0.0071  | ~22× |
| 256  | 0.05    | 1.363   | 0.118   | ~12× |

**新 CSR-SpMM vs 把 Â 当稠密做 GEMM：**

| n    | density | spmm (ms) | dense (ms) | 加速 |
| ---- | ------- | --------- | ---------- | ---- |
| 512  | 0.01    | 0.12      | 10.4       | ~87× |
| 1024 | 0.01    | 0.42      | 33.2       | ~79× |
| 2048 | 0.05    | 8.34      | 133.8      | ~16× |

SpMM 开销随 nnz 增长、稠密 GEMM 随 n² 增长，所以**图越稀疏优势越大**。（说明：稠密 baseline 是朴素三重循环而非优化过的 BLAS。）

### 2. 指令级 — AVX2 SIMD

内层 AXPY / 点积用 AVX2 向量化（float 一次 8 个、double 一次 4 个），通过 `-DUSE_AVX` 开关可对照标量版。控制变量（锁定单线程），SpMM on 8000×8000，density=0.005，N=128：

| 版本      | 单线程时间 | 加速      |
| --------- | ---------- | --------- |
| 标量      | 15.55 ms   | 1.00×     |
| AVX2 SIMD | 4.62 ms    | **3.37×** |

正确性由 `gradcheck` 保证：SIMD 版与标量版数值等价（double 最大相对误差 3.4e-9）。

### 3. 线程级 — OpenMP

行级并行（`#pragma omp parallel for`），每行独占写出，无数据竞争，并行结果与串行逐位一致（同一行由单线程顺序计算，浮点加法顺序不变）。同一配置下 SIMD 版的多线程扩展：

| 线程数 | 时间 (ms) | 加速  | 并行效率 |
| ------ | --------- | ----- | -------- |
| 1      | 4.62      | 1.00× | 100%     |
| 2      | 3.04      | 1.52× | 76%      |
| 4      | 2.02      | 2.28× | 57%      |
| 8      | 1.63      | 2.83× | 35%      |

**端到端（标量单线程 → SIMD + 8 线程）：约 9.5×。**

### 关键观察：优化计算会把瓶颈推向内存带宽

并行效率随线程数下降（8 线程仅 35%），说明 SpMM 在此配置下是 **memory-bound** 的——多线程很快撞到内存带宽天花板。

更有意思的是对照标量版：标量版 8 线程效率约 51%，SIMD 版只有 35%。原因是 **SIMD 让单线程计算变快后，内存带宽更早成为瓶颈**，于是并行扩展性反而变差——这是 HPC 里典型的"优化一个瓶颈，下一个瓶颈随即暴露"。此外，SpMM 的 memory-bound 程度取决于稠密侧宽度 N：N 越大计算密度越高，SIMD 收益越明显（本例 N=128 时 SIMD 仍有 3.37× 提升）。

> 数字为单机实测（CPU: 12th Gen Intel Core i7-12700H，GCC 8.1.0），仅供说明趋势；不同硬件绝对值会变，但"效率随线程下降""SIMD 后瓶颈转移"的规律稳定。

------

## 正确性：数值梯度检验（`gradcheck.cpp`）

用 `double` + 中心差分对照解析梯度，覆盖 GCN 与 GAT 的全部参数。GAT 六个梯度（每层 W / alpha_src / alpha_dst）：

```
W1  rel 1.2e-08    W2  rel 6.3e-09
AS1 rel 1.1e-08    AS2 rel 1.1e-09
AD1 rel 5.7e-09    AD2 rel 1.5e-09
-> PASS
```

> **一处假 FAIL 的诊断**：某随机种子下 alpha_src 的一个接近零的梯度分量，因相对误差分母趋零而虚高到 2.6e-05（catastrophic cancellation）。通过"改 eps 无效、换种子消失、绝对误差始终 ~1e-11"定位为数值噪声而非 bug，改用 rel-or-abs 判定根治。这也说明纯相对误差的梯度检验会被接近零的梯度分量误伤。

------

## 训练与结果

### Cora 节点分类（`train_cora.cpp`）

两层网络（hidden=16），AdamW（`lr=0.01, wd=5e-4`）+ Dropout 0.5，transductive 全图前向、仅在 train 掩码（140 节点）上回传。下载原始 LINQS 格式的 `cora.content` 与 `cora.cites`（GitHub 多有镜像）后：

```bash
g++ train_cora.cpp -o train -std=c++17 -O2 -mavx2 -mfma
./train cora/cora.content cora/cora.cites
```

不带数据集参数则回退到合成数据集（无需下载即可验证管线）。

**GCN vs GAT 对比（真实 Cora，相同超参）：**

| 模型        | Test Acc | train/val 差距 |
| ----------- | -------- | -------------- |
| GCN         | 78.5%    | ~19%           |
| GAT（单头） | 76.1%    | ~24%           |

与原论文同量级（GCN 81.5% / GAT 83%）。相同超参下 GAT 略低于 GCN，且**过拟合更明显**——GAT 参数更多，在 Cora 仅 140 个训练节点的小训练集上更易过拟合。GAT 要发挥优势需更强正则（dropout 0.6）、多头注意力，以及在邻居异质的图上才能体现注意力的价值。这印证了"模型复杂度需匹配数据规模"，而非越复杂越好。

### 优化器对照（合成 Cora-like 数据，可复现）

Cora 的特征是高维稀疏词袋、行归一化后量级极小，导致梯度过小、纯 SGD 几乎不动——这是 GNN 普遍使用 Adam 的原因。在同款 regime 的合成集上可清楚复现：

| 优化器 | 学习率 | 200 epoch 后 train_loss | 测试准确率 |
| ------ | ------ | ----------------------- | ---------- |
| SGD    | 0.2    | 1.946 → 1.945（爬不动） | 0.29       |
| AdamW  | 0.01   | 1.946 → 0.033           | 0.99       |

（合成集干净可分，故达 ~99%；真实 Cora 噪声更大，约 78%。）

------

## 编译与运行

环境：C++17，g++ / clang++。涉及 SIMD 的目标需 `-mavx2 -mfma`，涉及并行的需 `-fopenmp`。

```bash
# 训练（Cora 或合成回退）
g++ train_cora.cpp -o train     -std=c++17 -O2 -mavx2 -mfma
g++ train_cora.cpp -o train     -std=c++17 -O2 -mavx2 -mfma -fopenmp   # 启用并行 SpMM
OMP_NUM_THREADS=8 ./train cora.content cora.cites

# 梯度检验（double）
g++ gradcheck.cpp  -o gradcheck -std=c++17 -O2 -mavx2 -mfma && ./gradcheck

# 性能基准
g++ bench_spmm.cpp -o bench     -std=c++17 -O2 -mavx2 -mfma && ./bench
g++ bench_omp.cpp  -o bench_omp -std=c++17 -O2 -mavx2 -mfma -fopenmp && ./bench_omp
```

- `-mavx2 -mfma`：告诉编译器目标 CPU 支持的指令集（用了 AVX intrinsic 必须加，否则编译失败）。
- `-fopenmp`：开启 OpenMP 并行（不加则 pragma 被忽略、退化为串行，但仍能编译）。
- `-DUSE_AVX`：对照开关，加上走 SIMD 路径，不加走标量路径。

> 需 **C++17**（`Tensor.h` / `Dataset.h` 使用了结构化绑定等特性）。

------

## 实现要点

- **存储**：稠密用扁平 row-major 缓冲；稀疏用 CSR。两者为静态类型，无运行时密度切换。
- **算子**：`matmul`（ikj 顺序，cache 友好）、`matmul_AtB` / `matmul_ABt`（避免显式转置）、`CSR::spmm`（AXPY kernel，AVX2 + OpenMP）、边上 SDDMM、`CSR::transpose`、segment softmax。
- **训练**：Xavier 初始化、AdamW（per-parameter，weight 与注意力参数 a_src/a_dst 各自维护状态）、Inverted Dropout（缓存 mask 供反向）、masked 损失与准确率。
- **可复现**：全局固定种子的 RNG。

## 局限与后续方向

- 全图（full-batch）transductive 训练，未做 mini-batch / 邻居采样（大图会受内存限制）。
- GAT 目前为**单头**注意力，未实现 multi-head；超参沿用 GCN 配方，未单独为 GAT 调优。
- SIMD 仅 AVX2，未做 AVX-512 / CUDA。

后续可扩展：multi-head GAT、为 GAT 单独调参（更高 dropout + early stopping）、CUDA SpMM/SDDMM、邻居采样的 mini-batch 训练。

## 参考

- Kipf & Welling, *Semi-Supervised Classification with Graph Convolutional Networks* (ICLR 2017)
- Veličković et al., *Graph Attention Networks* (ICLR 2018)
- Cora 数据集（LINQS）

## 作者

聂宇航

