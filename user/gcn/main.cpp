#include "GCNLayer.h"
#include "Func.h"
#include <cstdio>
#include <vector>

using namespace std;

// Phase 2：teacher–student 训练，跑在重写后的核心上（验证算子改写未破坏训练）
int main() {
    const int N = 16, n = 12, F_in = 8, F_hidden = 16, F_out = 4;
    const int max_epoch = 300;
    const float lr = 0.05f;
    seed_rng(42);

    // 教师权重（固定）
    DenseMatrix<float> tW1(F_in, F_hidden), tW2(F_hidden, F_out);
    init_xavier(tW1); init_xavier(tW2);

    // 数据集：每图的 Â、X，以及教师生成的 Y
    vector<CSR<float>> Ahat(N);
    vector<DenseMatrix<float>> X(N), Y(N);
    {
        GCNLayer<float> t1(tW1), t2(tW2);
        for (int i = 0; i < N; ++i) {
            auto edges = random_symmetric_edges(n, 0.3);
            Ahat[i] = build_normalized_adj<float>(n, edges);
            X[i] = DenseMatrix<float>(n, F_in);
            init_gaussian(X[i]);
            t1.set_adj(Ahat[i]); t2.set_adj(Ahat[i]);
            Y[i] = t2.forward(t1.forward(X[i]));
        }
    }

    // 学生网络
    DenseMatrix<float> sW1(F_in, F_hidden), sW2(F_hidden, F_out);
    init_xavier(sW1); init_xavier(sW2);
    GCNLayer<float> layer1(sW1), layer2(sW2);

    vector<float> curve;
    curve.reserve(max_epoch);
    for (int epoch = 0; epoch < max_epoch; ++epoch) {
        float epoch_loss = 0.f;
        for (int i = 0; i < N; ++i) {
            layer1.set_adj(Ahat[i]); layer2.set_adj(Ahat[i]);
            DenseMatrix<float> out = layer2.forward(layer1.forward(X[i]));
            epoch_loss += MSELoss(out, Y[i]);
            DenseMatrix<float> dOut = LossGrad(out, Y[i]);
            layer1.backward(layer2.backward(dOut));
            layer1.updateWeights(lr);
            layer2.updateWeights(lr);
        }
        curve.push_back(epoch_loss / N);
    }

    printf("epoch | avg MSE loss\n");
    for (int e = 0; e < max_epoch; ++e)
        if (e % 20 == 0 || e == max_epoch - 1)
            printf("%5d | %.6f\n", e, curve[e]);
    return 0;
}
