#include "GCNLayer.h"
#include "Func.h"
#include "Dataset.h"
#include "Optim.h"
#include "safetensors.h"
#include <string>
#include <vector>
#include <stdio.h>

using T = float;

void train(Graph<T>& g, int hidden, int epochs, T lr, T wd, bool use_adam, T drop_p,
           const char* save_path = nullptr) {
    DenseMatrix<T> W1(g.X.cols, hidden), W2(hidden, g.num_classes);
    init_xavier(W1); init_xavier(W2);
    GCNLayer<T> L1(W1, GCNLayer<T>::Activation::Relu);
    GCNLayer<T> L2(W2, GCNLayer<T>::Activation::Identity);
    L1.set_adj(g.Ahat); L2.set_adj(g.Ahat);

    Adam<T> opt1(W1.rows, W1.cols, lr, wd), opt2(W2.rows, W2.cols, lr, wd);
    Dropout<T> din(drop_p), dhid(drop_p);

    auto eval = [&]() { return L2.forward(L1.forward(g.X)); };  // 推理：无 dropout

    printf("optimizer=%s lr=%g wd=%g dropout=%g\n", use_adam ? "AdamW" : "SGD", lr, wd, drop_p);
    printf("epoch | train_loss | train_acc | val_acc\n");
    for (int e = 0; e < epochs; ++e) {
        // 训练前向（带 dropout）
        DenseMatrix<T> Xd = din.forward(g.X, true);
        DenseMatrix<T> h  = L1.forward(Xd);
        DenseMatrix<T> hd = dhid.forward(h, true);
        DenseMatrix<T> logits = L2.forward(hd);
        DenseMatrix<T> P = segment_softmax (logits);

        T loss = cross_entropy_masked(P, g.labels, g.train_mask);
        DenseMatrix<T> dlogits = softmax_ce_grad(P, g.labels, g.train_mask);
        DenseMatrix<T> dhd = L2.backward(dlogits);
        DenseMatrix<T> dh  = dhid.backward(dhd);   // dropout 反向：同一 mask
        L1.backward(dh);

        if (use_adam) { opt1.step(L1.weight, L1.gradWeight); opt2.step(L2.weight, L2.gradWeight); }
        else          { L1.updateWeights(lr); L2.updateWeights(lr); }

        if (e % 20 == 0 || e == epochs - 1) {
            DenseMatrix<T> lg = eval();
            printf("%5d | %10.4f | %8.3f | %7.3f\n", e, loss,
                   accuracy_masked(lg, g.labels, g.train_mask),
                   accuracy_masked(lg, g.labels, g.val_mask));
        }
    }
    DenseMatrix<T> lg = eval();
    printf("\n==> final TEST accuracy = %.4f\n", accuracy_masked(lg, g.labels, g.test_mask));

    // ---- 保存权重（推理侧从这里接）----
    if (save_path) {
        st::Entry es[2] = {{"layer1.weight", &L1.weight}, {"layer2.weight", &L2.weight}};
        // 元数据记下形状：推理时用它自证"模型与数据是否配套"
        char meta[256];
        snprintf(meta, sizeof meta,
                 "{\"arch\":\"gcn-2layer\",\"features\":\"%d\",\"hidden\":\"%d\",\"classes\":\"%d\"}",
                 g.X.cols, L1.weight.cols, g.num_classes);
        std::string err;
        if (st::save(save_path, es, 2, meta, err)) {
            printf("已保存权重 -> %s (layer1.weight %dx%d, layer2.weight %dx%d)\n",
                   save_path, L1.weight.rows, L1.weight.cols, L2.weight.rows, L2.weight.cols);
            printf("提示: 落盘由内核周期 writeback 完成; 需要立即持久化可在保存后执行 sync\n");
        } else {
            printf("保存失败: %s\n", err.c_str());
        }
    }
}

int main(int argc, char** argv) {
    seed_rng(42);
    std::vector<std::string> files;
    const char* save_path = nullptr;
    bool use_adam = true;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "sgd") use_adam = false;
        else if (a == "adam") use_adam = true;
        else if (a == "--save" && i + 1 < argc) save_path = argv[++i];
        else if (a.rfind("--save=", 0) == 0) save_path = a.c_str() + 7;
        else files.push_back(a);
    }

    Graph<T> g;
    if (files.size() >= 2) { g = load_cora<T>(files[0], files[1]); printf("[dataset] Cora: "); }
    else { g = make_cora_like<T>(7, 200, 600, 30, 0.04, 0.004, 0.7); printf("[dataset] Cora-like synthetic: "); }
    if (g.n == 0) { printf("数据集为空\n"); return 1; }

    int ntr = 0, nv = 0, nte = 0;
    for (int i = 0; i < g.n; ++i) { ntr += g.train_mask[i]; nv += g.val_mask[i]; nte += g.test_mask[i]; }
    printf("nodes=%d features=%d classes=%d nnz=%d | train=%d val=%d test=%d\n\n",
           g.n, g.X.cols, g.num_classes, g.Ahat.nnz(), ntr, nv, nte);

    // Cora 推荐：AdamW lr=0.01 wd=5e-4 dropout=0.5 hidden=16
    train(g, /*hidden=*/16, /*epochs=*/200,
          /*lr=*/use_adam ? T(0.01) : T(0.2),
          /*wd=*/use_adam ? T(5e-4) : T(0),
          use_adam, /*drop_p=*/T(0.5), save_path);
    return 0;
}
