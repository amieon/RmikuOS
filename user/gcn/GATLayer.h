#pragma once
#include "Tensor.h"
#include "Func.h"

template<typename T>
class GATLayer {
public:
    enum class Activation { Relu, Identity };  // 末层用 Identity 输出 logits

    DenseMatrix<T> weight;
    DenseMatrix<T> gradWeight;
    CSR<T> adjHat, adjHatT;
    CSR<T> e_pre,alpha,alphaT;
    DenseMatrix<T> alpha_src,alpha_dst;
    DenseMatrix<T> dalpha_src,dalpha_dst;
    DenseMatrix<T> inputX, Z, outputH, XW;
    Activation act = Activation::Relu;

    GATLayer() = default;
    explicit GATLayer(const DenseMatrix<T>& W, Activation a = Activation::Relu)
            : weight(W), gradWeight(W.rows, W.cols),
              alpha_src(W.cols, 1), alpha_dst(W.cols, 1),   // 形状自己算，不靠外面
              act(a)
    {
        init_xavier(alpha_src);   // 初始化也在里面做
        init_xavier(alpha_dst);
    }

    void set_adj(const CSR<T>& a) { adjHat = a; adjHatT = a.transpose(); e_pre = alpha = adjHat; }

    DenseMatrix<T> forward(const DenseMatrix<T>& X) {
        // σ(sf(Lk(Z*ar + Z*ad))XW)

        inputX = X;
        XW = matmul(X, weight);

        DenseMatrix<T> tmp_src = matmul(XW,alpha_src);
        DenseMatrix<T> tmp_dst = matmul(XW,alpha_dst);

        for (int i = 0; i < adjHat.rows; ++i) {
            for (int idx = adjHat.row_ptr[i]; idx < adjHat.row_ptr[i+1]; ++idx) {
                e_pre.values[idx] = tmp_dst(adjHat.col_idx[idx],0) + tmp_src(i,0);
                if(e_pre.values[idx] < 0) e_pre.values[idx] *= 0.02;
            }
        }

        alpha = sparse_softmax(e_pre);
        alphaT = alpha.transpose();
        Z = alpha.spmm(XW);
        outputH = (act == Activation::Relu) ? relu(Z) : Z;
        return outputH;
    }

    DenseMatrix<T> backward(const DenseMatrix<T>& dH) {
        DenseMatrix<T> dZout = (act == Activation::Relu) ? hadamard(dH, relu_deriv(Z)) : dH;

        CSR<T> dalpha = alpha;
        for (int i = 0; i < dalpha.rows; ++i) {
            for (int idx = dalpha.row_ptr[i]; idx < dalpha.row_ptr[i+1]; ++idx) {
                int j = dalpha.col_idx[idx];
                T s = 0;
                for (int f = 0; f < XW.cols; ++f) s += dZout(i,f) * XW(j,f);
                dalpha.values[idx] = s;
            }
        }

        CSR<T> de_pre = alpha;
        for (int i = 0; i < de_pre.rows; ++i) {
            T rows_sum = T(0);
            for (int idx = de_pre.row_ptr[i]; idx < de_pre.row_ptr[i+1]; ++idx) {
                rows_sum += alpha.values[idx] * dalpha.values[idx];
            }
            for (int idx = de_pre.row_ptr[i]; idx < de_pre.row_ptr[i+1]; ++idx) {
                de_pre.values[idx] = (alpha.values[idx] * (dalpha.values[idx] - rows_sum)) * (e_pre.values[idx] > 0 ? 1 : 0.02);
            }
        }

        DenseMatrix<T> dsrc(adjHat.rows, 1),ddst(adjHat.cols, 1);
        for (int i = 0; i < de_pre.rows; ++i) {
            for (int idx = de_pre.row_ptr[i]; idx < de_pre.row_ptr[i+1]; ++idx) {
                dsrc(i,0) += de_pre.values[idx];
                ddst(de_pre.col_idx[idx],0) += de_pre.values[idx];
            }
        }


        dalpha_src = matmul_AtB(XW,dsrc);
        dalpha_dst = matmul_AtB(XW,ddst);

        DenseMatrix<T> dZ = alphaT.spmm(dZout) + matmul_ABt(ddst, alpha_dst) + matmul_ABt(dsrc, alpha_src);
        gradWeight = matmul_AtB(inputX, dZ);

        return matmul_ABt(dZ, weight);
    }

    void updateWeights(T lr) {
        axpy_update(weight, gradWeight, lr);
        axpy_update(alpha_src, dalpha_src, lr);
        axpy_update(alpha_dst, dalpha_dst, lr);
    }
};
