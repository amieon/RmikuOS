// gcn_infer —— GCN 推理：加载 safetensors 权重，只做前向，输出准确率。
//
// 训练与推理的分工：
//   train_cora --save model.safetensors [cora.content cora.cites]   ← 训完存盘
//   gcn_infer   --model model.safetensors [cora.content cora.cites]  ← 只做前向
//
// 推理省掉的东西：反向传播、优化器、dropout、以及几百轮的循环。
// 数据流水线与 train_cora 完全一致（同一个 load_cora，内部做行归一化 + 固定划分），
// 所以这里打出的准确率可以直接和训练结束时的数字对照 —— 这就是"训练/推理分离"的验收。
//
// 不给参数时用合成数据集（与 train_cora 默认一致），此时模型也必须是同一次训练存的。

#include "GCNLayer.h"
#include "Func.h"
#include "Dataset.h"
#include "safetensors.h"
#include <string>
#include <vector>
#include <stdio.h>

using T = float;

int main(int argc, char** argv) {
    const char* model_path = "model.safetensors";
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--model" && i + 1 < argc) model_path = argv[++i];
        else if (a.rfind("--model=", 0) == 0) model_path = a.c_str() + 8;
        else files.push_back(a);
    }

    // 必须与 train_cora 一样 seed：build_splits 里用 global_rng 做 shuffle，
    // 同一个种子才能复现同一套 train/val/test 划分，准确率才可比。
    seed_rng(42);

    Graph<T> g;
    if (files.size() >= 2) { g = load_cora<T>(files[0], files[1]); printf("[dataset] Cora: "); }
    else { g = make_cora_like<T>(7, 200, 600, 30, 0.04, 0.004, 0.7); printf("[dataset] synthetic: "); }
    if (g.n == 0) { printf("数据集为空\n"); return 1; }

    int ntr = 0, nv = 0, nte = 0;
    for (int i = 0; i < g.n; ++i) { ntr += g.train_mask[i]; nv += g.val_mask[i]; nte += g.test_mask[i]; }
    printf("nodes=%d features=%d classes=%d nnz=%d | train=%d val=%d test=%d\n",
           g.n, g.X.cols, g.num_classes, g.Ahat.nnz(), ntr, nv, nte);

    // ---- 加载权重 ----
    std::string err;
    st::File f;
    if (!st::load(model_path, f, err)) {
        printf("加载模型失败(%s): %s\n", model_path, err.c_str());
        return 1;
    }
    DenseMatrix<T> W1, W2;
    if (!st::get_f32(f, "layer1.weight", W1, err) ||
        !st::get_f32(f, "layer2.weight", W2, err)) {
        printf("取权重失败: %s\n", err.c_str());
        return 1;
    }

    // 元数据自证：保存时写进了 features/hidden/classes
    std::string v;
    if (st::meta_str(f, "features", v, err)) printf("[model] metadata: features=%s", v.c_str());
    if (st::meta_str(f, "hidden", v, err)) printf(" hidden=%s", v.c_str());
    if (st::meta_str(f, "classes", v, err)) printf(" classes=%s", v.c_str());
    if (st::meta_str(f, "arch", v, err)) printf(" arch=%s", v.c_str());
    printf("\n");

    // ---- 形状校验（早失败，别等算出错数字）----
    const int f_in = W1.rows, hidden = W1.cols, classes = W2.cols;
    printf("[model] layer1.weight=%dx%d layer2.weight=%dx%d\n", W1.rows, W1.cols, W2.rows, W2.cols);
    if (W2.rows != hidden) {
        printf("形状不匹配: layer2 输入维度 %d 与 hidden %d 不一致\n", W2.rows, hidden);
        return 1;
    }
    if (f_in != g.X.cols) {
        printf("形状不匹配: 模型 features=%d, 数据 features=%d\n", f_in, g.X.cols);
        printf("  (常见原因: 模型是用合成数据集训的, 这里却给了 Cora; 或反之)\n");
        return 1;
    }
    if (classes != g.num_classes) {
        printf("形状不匹配: 模型 classes=%d, 数据 classes=%d\n", classes, g.num_classes);
        return 1;
    }

    // ---- 组装网络，只做前向 ----
    GCNLayer<T> L1(W1, GCNLayer<T>::Activation::Relu);
    GCNLayer<T> L2(W2, GCNLayer<T>::Activation::Identity);
    L1.set_adj(g.Ahat);
    L2.set_adj(g.Ahat);

    DenseMatrix<T> logits = L2.forward(L1.forward(g.X));   // 推理：无 dropout、无反向

    printf("[infer] train_acc=%.4f val_acc=%.4f test_acc=%.4f\n",
           accuracy_masked(logits, g.labels, g.train_mask),
           accuracy_masked(logits, g.labels, g.val_mask),
           accuracy_masked(logits, g.labels, g.test_mask));
    printf("(对照: 训练结束时打印的 final TEST accuracy 应当与此一致)\n");
    return 0;
}
