#pragma once
#include <iostream>
#include <vector>
#include <cassert>

// CSR Matrix Definition
template<typename T>
struct CSRMatrix {
    int rows, cols;
    std::vector<int> row_ptr;
    std::vector<int> col_idx;
    std::vector<T> values;
};

template<typename T>
class Matrix {
public:
    enum class Mode { SPARSE, DENSE };

    int rows, cols;
    int nnz = 0;
    double threshold = 0.1;
    Mode mode = Mode::SPARSE;

    CSRMatrix<T> csr_data;
    std::vector<std::vector<T>> dense_data;

    Matrix(){
        rows = 0,cols = 0;
    }
    Matrix(int r, int c) : rows(r), cols(c) {
        csr_data.rows = r;
        csr_data.cols = c;
        csr_data.row_ptr.resize(r + 1, 0);
    }

    T get(int i, int j) const {
        if (mode == Mode::DENSE)
            return dense_data[i][j];
        else {
            for (int idx = csr_data.row_ptr[i]; idx < csr_data.row_ptr[i + 1]; ++idx) {
                if (csr_data.col_idx[idx] == j)
                    return csr_data.values[idx];
            }
            return T{};
        }
    }

    void setSparseCSR(int i, int j, T val) {
        int row_start = csr_data.row_ptr[i];
        int row_end = csr_data.row_ptr[i + 1];

        for (int idx = csr_data.row_ptr[i]; idx < csr_data.row_ptr[i + 1]; ++idx) {
            if (csr_data.col_idx[idx] == j) {
                if (val == T{}) {
                    csr_data.col_idx.erase(csr_data.col_idx.begin() + idx);
                    csr_data.values.erase(csr_data.values.begin() + idx);
                    for (int k = i + 1; k <= rows; ++k)
                        csr_data.row_ptr[k]--;
                    nnz--;
                } else {
                    csr_data.values[idx] = val;
                }
                return;
            }
        }

        if (val != T{}) {
            int insert_pos = row_end;
            while (insert_pos > row_start && csr_data.col_idx[insert_pos - 1] > j)
                --insert_pos;

            csr_data.col_idx.insert(csr_data.col_idx.begin() + insert_pos, j);
            csr_data.values.insert(csr_data.values.begin() + insert_pos, val);
            for (int k = i + 1; k <= rows; ++k)
                csr_data.row_ptr[k]++;
            nnz++;
        }
    }

    void set(int i, int j, T val) {
        assert(i >= 0 && i < rows && j >= 0 && j < cols);
        if (mode == Mode::DENSE)
            dense_data[i][j] = val;
        else
            setSparseCSR(i, j, val);
        checkModeSwitch();
    }

    void checkModeSwitch() {
        double density = static_cast<double>(nnz) / (rows * cols);
        if (mode == Mode::SPARSE && density >= threshold) switchToDense();
        else if (mode == Mode::DENSE && density < threshold) switchToSparse();
    }

    void switchToDense() {
        dense_data.assign(rows, std::vector<T>(cols, T{}));
        for (int i = 0; i < rows; ++i) {
            for (int k = csr_data.row_ptr[i]; k < csr_data.row_ptr[i + 1]; ++k) {
                int j = csr_data.col_idx[k];
                dense_data[i][j] = csr_data.values[k];
            }
        }
        csr_data = CSRMatrix<T>();
        mode = Mode::DENSE;
    }

    void switchToSparse() {
        csr_data = CSRMatrix<T>();
        csr_data.rows = rows;
        csr_data.cols = cols;
        csr_data.row_ptr.resize(rows + 1, 0);
        for (int i = 0; i < rows; ++i)
            for (int j = 0; j < cols; ++j)
                if (dense_data[i][j] != T{})
                    setSparseCSR(i, j, dense_data[i][j]);
        dense_data.clear();
        mode = Mode::SPARSE;
    }

    void print() const {
        for (int i = 0; i < rows; ++i) {
            for (int j = 0; j < cols; ++j)
                std::cout << get(i, j) << " ";
            std::cout << "\n";
        }
    }

    CSRMatrix<T> toCSR() const {
        assert(mode == Mode::SPARSE);
        return csr_data;
    }

    Matrix<T> transpose() const {
        Matrix<T> result(cols, rows);
        if (mode == Mode::DENSE) {
            result.mode = Mode::DENSE;
            result.dense_data.assign(cols, std::vector<T>(rows, T{}));
            for (int i = 0; i < rows; ++i)
                for (int j = 0; j < cols; ++j)
                    result.dense_data[j][i] = dense_data[i][j];
            result.nnz = nnz;
        } else {
            std::vector<std::vector<std::pair<int, T>>> tmp(cols);
            for (int i = 0; i < rows; ++i) {
                for (int k = csr_data.row_ptr[i]; k < csr_data.row_ptr[i + 1]; ++k) {
                    int j = csr_data.col_idx[k];
                    T val = csr_data.values[k];
                    tmp[j].emplace_back(i, val);
                }
            }
            result.mode = Mode::SPARSE;
            result.nnz = nnz;
            result.csr_data.rows = cols;
            result.csr_data.cols = rows;
            result.csr_data.row_ptr.resize(cols + 1, 0);
            for (int i = 0; i < cols; ++i) {
                result.csr_data.row_ptr[i + 1] = result.csr_data.row_ptr[i] + tmp[i].size();
                for (auto [j, val] : tmp[i]) {
                    result.csr_data.col_idx.push_back(j);
                    result.csr_data.values.push_back(val);
                }
            }
        }
        return result;
    }

    static Matrix<T> fromCSR(const CSRMatrix<T>& csr) {
        Matrix<T> mat(csr.rows, csr.cols);
        mat.mode = Mode::SPARSE;
        mat.nnz = csr.values.size();
        mat.csr_data = csr;
        return mat;
    }

    static CSRMatrix<T> csrMultiplyCSR(const CSRMatrix<T>& A, const CSRMatrix<T>& B) {
        assert(A.cols == B.rows);
        CSRMatrix<T> C;
        C.rows = A.rows;
        C.cols = B.cols;
        std::vector<int> row_ptr(C.rows + 1, 0);
        std::vector<int> col_buffer(C.cols, -1);
        std::vector<T> val_buffer(C.cols, T{});
        std::vector<int> temp_cols;

        for (int i = 0; i < A.rows; ++i) {
            temp_cols.clear();
            for (int idx_a = A.row_ptr[i]; idx_a < A.row_ptr[i + 1]; ++idx_a) {
                int k = A.col_idx[idx_a];
                T a_val = A.values[idx_a];
                for (int idx_b = B.row_ptr[k]; idx_b < B.row_ptr[k + 1]; ++idx_b) {
                    int j = B.col_idx[idx_b];
                    T b_val = B.values[idx_b];
                    if (col_buffer[j] != i) {
                        col_buffer[j] = i;
                        val_buffer[j] = a_val * b_val;
                        temp_cols.push_back(j);
                    } else {
                        val_buffer[j] += a_val * b_val;
                    }
                }
            }
            for (int j : temp_cols) {
                C.col_idx.push_back(j);
                C.values.push_back(val_buffer[j]);
            }
            row_ptr[i + 1] = (int)C.values.size();
        }
        C.row_ptr = row_ptr;
        return C;
    }

    friend Matrix<T> operator+(const Matrix<T>& A, const Matrix<T>& B){
        assert(A.cols == B.cols && A.rows == B.rows);
        Matrix<T> out(A.rows, A.cols);
        for (int i = 0; i < A.rows; ++i)
            for (int j = 0; j < A.cols; ++j)
                out.set(i,j,A.get(i,j)+B.get(i,j));
        return out;
    }
    friend Matrix<T> operator-(const Matrix<T>& A, const Matrix<T>& B){
        assert(A.cols == B.cols && A.rows == B.rows);
        Matrix<T> out(A.rows, A.cols);
        for (int i = 0; i < A.rows; ++i)
            for (int j = 0; j < A.cols; ++j)
                out.set(i,j,A.get(i,j)-B.get(i,j));
        return out;
    }
    friend Matrix<T> operator*(const Matrix<T>& A, T val){
        Matrix<T> out(A.rows, A.cols);
        for (int i = 0; i < A.rows; ++i)
            for (int j = 0; j < A.cols; ++j)
                out.set(i,j,A.get(i,j)*val);
        return out;
    }
    friend Matrix<T> operator*(T val, const Matrix<T>& B){
        return B * val;
    }
    friend Matrix<T> operator%(const Matrix<T>& A, const Matrix<T>& B) {
        assert(A.cols == B.cols && A.rows == B.rows);
        Matrix<T> result(A.rows, A.cols);
        for (int i = 0; i < A.rows; ++i)
            for (int j = 0; j < A.cols; ++j)
                result.set(i,j,A.get(i,j) * B.get(i,j));
        return result;
    }

    friend Matrix<T> operator*(const Matrix<T>& A, const Matrix<T>& B) {
        assert(A.cols == B.rows);
        Matrix<T> result(A.rows, B.cols);
        if (A.mode == Mode::SPARSE && B.mode == Mode::DENSE) {
            CSRMatrix<T> A_csr = A.toCSR();
            for (int i = 0; i < A_csr.rows; ++i) {
                for (int idx = A_csr.row_ptr[i]; idx < A_csr.row_ptr[i + 1]; ++idx) {
                    int k = A_csr.col_idx[idx];
                    T val = A_csr.values[idx];
                    for (int j = 0; j < B.cols; ++j)
                        result.set(i, j, result.get(i, j) + val * B.get(k, j));
                }
            }
        } else if (A.mode == Mode::DENSE && B.mode == Mode::DENSE) {
            for (int i = 0; i < A.rows; ++i)
                for (int k = 0; k < A.cols; ++k)
                    for (int j = 0; j < B.cols; ++j)
                        result.set(i, j, result.get(i, j) + A.dense_data[i][k] * B.dense_data[k][j]);
        } else if (A.mode == Mode::DENSE && B.mode == Mode::SPARSE) {
            for (int i = 0; i < A.rows; ++i) {
                for (int k = 0; k < A.cols; ++k) {
                    T a_val = A.dense_data[i][k];
                    for (int j = 0; j < B.cols; ++j) {
                        T b_val = B.get(k, j);
                        if (b_val != T{})
                            result.set(i, j, result.get(i, j) + a_val * b_val);
                    }
                }
            }
        } else if (A.mode == Mode::SPARSE && B.mode == Mode::SPARSE) {
            CSRMatrix<T> A_csr = A.toCSR();
            CSRMatrix<T> B_csr = B.toCSR();
            CSRMatrix<T> C_csr = csrMultiplyCSR(A_csr, B_csr);
            return fromCSR(C_csr);
        }
        result.checkModeSwitch();
        return result;
    }

};
