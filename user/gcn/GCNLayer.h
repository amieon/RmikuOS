#pragma once
#include "Tensor.h"
#include "my/stdcompat.h"


template<typename T>
class GCNLayer {
public:
    enum class Activation { Relu, Identity };  // 末层用 Identity 输出 logits

    DenseMatrix<T> weight;
    DenseMatrix<T> gradWeight;
    CSR<T> adjHat, adjHatT;
    DenseMatrix<T> inputX, Z, outputH;
    Activation act = Activation::Relu;

    GCNLayer() = default;
    explicit GCNLayer(const DenseMatrix<T>& W, Activation a = Activation::Relu)
        : weight(W), gradWeight(W.rows, W.cols), act(a) {}

    void set_adj(const CSR<T>& a) { adjHat = a; adjHatT = a.transpose(); }

    DenseMatrix<T> forward(const DenseMatrix<T>& X) {
        inputX = X;
        DenseMatrix<T> XW = matmul(X, weight);
        Z = adjHat.spmm(XW);
        outputH = (act == Activation::Relu) ? relu(Z) : Z;
        return outputH;
    }

    DenseMatrix<T> backward(const DenseMatrix<T>& dH) {
        DenseMatrix<T> dZ = (act == Activation::Relu) ? hadamard(dH, relu_deriv(Z)) : dH;
        DenseMatrix<T> dM = adjHatT.spmm(dZ);        // dM = Â^T dZ
        gradWeight = matmul_AtB(inputX, dM);         // dW = X^T dM
        DenseMatrix<T> dX = matmul_ABt(dM, weight);  // dX = dM W^T
        return dX;
    }

    void updateWeights(T lr) { axpy_update(weight, gradWeight, lr); }
};
