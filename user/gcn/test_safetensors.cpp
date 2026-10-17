#include "safetensors.h"
#include <stdio.h>

int main() {
    // 两个小权重矩阵，形状刻意不同，用来检查 shape/offsets 是否各自正确
    DenseMatrix<float> W1(3, 4), W2(4, 2);
    for (int i = 0; i < 12; ++i) W1.data[i] = (float)i * 0.5f;
    for (int i = 0; i < 8; ++i) W2.data[i] = -1.25f * (float)i;

    std::string err;

    st::Entry entries[2] = {{"layer1.weight", &W1}, {"layer2.weight", &W2}};
    if (!st::save("model.safetensors", entries, 2, 0, err)) {
        printf("save 失败: %s\n", err.c_str());
        return 1;
    }
    printf("写出 model.safetensors OK\n");

    st::File f;
    if (!st::load("model.safetensors", f, err)) {
        printf("load 失败: %s\n", err.c_str());
        return 1;
    }
    printf("读到 %d 个张量, 数据区 %d 字节\n", (int)f.tensors.size(), (int)f.data.size());
    for (size_t i = 0; i < f.tensors.size(); ++i) {
        const st::TensorInfo& t = f.tensors[i];
        printf("  %-16s dtype=%s shape=[%lld,%lld] offsets=[%llu,%llu]\n",
               t.name.c_str(), t.dtype.c_str(),
               t.shape[0], t.shape[1], t.begin, t.end);
    }

    DenseMatrix<float> R1, R2;
    if (!st::get_f32(f, "layer1.weight", R1, err) ||
        !st::get_f32(f, "layer2.weight", R2, err)) {
        printf("取张量失败: %s\n", err.c_str());
        return 1;
    }

    int bad = 0;
    for (int i = 0; i < 12; ++i) if (R1.data[i] != W1.data[i]) ++bad;
    for (int i = 0; i < 8; ++i) if (R2.data[i] != W2.data[i]) ++bad;
    printf("回读比对: 不一致 %d 个\n", bad);
    printf("抽样: W1[1][2]=%g (期望 3), W2[3][1]=%g (期望 -8.75)\n", R1(1, 2), R2(3, 1));

    // 负向用例：读一个不存在的张量，应当报错而不是崩溃
    DenseMatrix<float> dummy;
    if (st::get_f32(f, "no.such.tensor", dummy, err)) {
        printf("负向用例失败: 不存在的张量竟然读成功了\n");
        return 1;
    }
    printf("负向用例: 正确报错 -> %s\n", err.c_str());

    // __metadata__ 往返：写带元数据的文件，再读回来
    if (!st::save("model_meta.safetensors", entries, 2,
                  "{\"arch\":\"gcn-2layer\",\"features\":\"1433\",\"hidden\":\"16\",\"classes\":\"7\"}",
                  err)) {
        printf("带 metadata 保存失败: %s\n", err.c_str());
        return 1;
    }
    st::File fm;
    if (!st::load("model_meta.safetensors", fm, err)) {
        printf("带 metadata 读取失败: %s\n", err.c_str());
        return 1;
    }
    std::string v;
    bool meta_ok = st::meta_str(fm, "features", v, err) && v == "1433" &&
                   st::meta_str(fm, "hidden", v, err) && v == "16" &&
                   st::meta_str(fm, "classes", v, err) && v == "7";
    printf("metadata 往返: %s (features=%s)\n", meta_ok ? "OK" : "失败", v.c_str());
    if (!st::meta_str(fm, "不存在的键", v, err)) {
        printf("metadata 负向用例: 正确报错 -> %s\n", err.c_str());
    }
    if (!meta_ok) return 1;

    if (bad) {
        printf("自测 FAIL\n");
        return 1;
    }
    printf("自测 PASS\n");
    return 0;
}
