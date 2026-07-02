#include "GATLayer.h"
#include "Func.h"
#include "Dataset.h"
#include "Optim.h"
#include "my/stdcompat.h"

using T = float;

void train(Graph<T>& g, int hidden, int epochs, T lr, T wd, bool use_adam, T drop_p) {
    DenseMatrix<T> W1(g.X.cols, hidden), W2(hidden, g.num_classes);
    init_xavier(W1); init_xavier(W2);
    GATLayer<T> L1(W1, GATLayer<T>::Activation::Relu);
    GATLayer<T> L2(W2, GATLayer<T>::Activation::Identity);
    L1.set_adj(g.Ahat); L2.set_adj(g.Ahat);

    Adam<T> opt1(W1.rows, W1.cols, lr, wd), opt2(W2.rows, W2.cols, lr, wd);
    Adam<T> opt_as1(W1.cols, 1, lr, wd),opt_as2(W2.cols, 1, lr, wd);
    Adam<T> opt_ad1(W1.cols, 1, lr, wd),opt_ad2(W2.cols, 1, lr, wd);


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

        if (use_adam) {
            opt1.step(L1.weight, L1.gradWeight); opt2.step(L2.weight, L2.gradWeight);
            opt_as1.step(L1.alpha_src, L1.dalpha_src); opt_as2.step(L2.alpha_src, L2.dalpha_src);
            opt_ad1.step(L1.alpha_dst, L1.dalpha_dst); opt_ad2.step(L2.alpha_dst, L2.dalpha_dst);
        }
        else {
            L1.updateWeights(lr);
            L2.updateWeights(lr);
        }

        if (e % 20 == 0 || e == epochs - 1) {
            DenseMatrix<T> lg = eval();
            printf("%5d | %10.4f | %8.3f | %7.3f\n", e, loss,
                   accuracy_masked(lg, g.labels, g.train_mask),
                   accuracy_masked(lg, g.labels, g.val_mask));
        }
    }
    DenseMatrix<T> lg = eval();
    printf("\n==> final TEST accuracy = %.4f\n", accuracy_masked(lg, g.labels, g.test_mask));
}

int main(int argc, char** argv) {
    seed_rng(42);
    std::vector<std::string> files;
    bool use_adam = true;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "sgd") use_adam = false;
        else if (a == "adam") use_adam = true;
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
          use_adam, /*drop_p=*/T(0.5));
    return 0;
}
