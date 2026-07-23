#include "mem.h"
#include "my/vector.h"   


extern "C" {
    long syscall3(unsigned long, unsigned long, unsigned long, unsigned long);
}
static void puts_raw(const char* s){unsigned long n=0;while(s[n])n++;syscall3(2,1,(unsigned long)s,n);}
static void put_int(long v){
    char b[24];int n=0;
    if(v==0)b[n++]='0';
    int neg=v<0; if(neg)v=-v;
    char t[24];int k=0;
    while(v>0){t[k++]=char('0'+v%10);v/=10;}
    if(neg)b[n++]='-';
    while(k>0)b[n++]=t[--k];
    b[n++]='\n';b[n]=0;
    puts_raw(b);
}

// ---- 最小 DenseMatrix(data 用 MyVector)----
// 这就是把 Tensor.h 的 std::vector 换成 mv::Vector,其余不变。
template<typename T>
struct DenseMatrix {
    int rows = 0, cols = 0;
    mv::Vector<T> data;   // row-major,连续存储

    DenseMatrix() = default;
    DenseMatrix(int r, int c) : rows(r), cols(c), data((unsigned long)r * c, T{}) {}

    T&       operator()(int i, int j)       { return data[(unsigned long)i * cols + j]; }
    const T& operator()(int i, int j) const { return data[(unsigned long)i * cols + j]; }

    T*       row_ptr(int i)       { return &data[(unsigned long)i * cols]; }
    const T* row_ptr(int i) const { return &data[(unsigned long)i * cols]; }
};

// ---- ikj matmul(原样照搬 Tensor.h,去掉 AVX,纯标量)----
template<typename T>
DenseMatrix<T> matmul(const DenseMatrix<T>& A, const DenseMatrix<T>& B) {
    const int M = A.rows, K = A.cols, N = B.cols;
    DenseMatrix<T> C(M, N);
    for (int i = 0; i < M; ++i) {
        T* crow = C.row_ptr(i);
        const T* arow = A.row_ptr(i);
        for (int k = 0; k < K; ++k) {
            const T a = arow[k];
            const T* brow = B.row_ptr(k);
            for (int j = 0; j < N; ++j)
                crow[j] += a * brow[j];        // AXPY
        }
    }
    return C;
}

extern "C" int main() {
    puts_raw("matmul test (MyVector-backed DenseMatrix)\n");

    // ---- 测试1:整数小矩阵,手算可验证 ----
    // A = [[1,2,3],
    //      [4,5,6]]   (2x3)
    // B = [[7, 8],
    //      [9, 10],
    //      [11,12]]   (3x2)
    // C = A*B = [[1*7+2*9+3*11, 1*8+2*10+3*12],     = [[58,  64],
    //            [4*7+5*9+6*11, 4*8+5*10+6*12]]        [139, 154]]
    DenseMatrix<int> A(2, 3);
    A(0,0)=1; A(0,1)=2; A(0,2)=3;
    A(1,0)=4; A(1,1)=5; A(1,2)=6;

    DenseMatrix<int> B(3, 2);
    B(0,0)=7;  B(0,1)=8;
    B(1,0)=9;  B(1,1)=10;
    B(2,0)=11; B(2,1)=12;

    DenseMatrix<int> C = matmul(A, B);

    puts_raw("C[0][0]="); put_int(C(0,0));   // 58
    puts_raw("C[0][1]="); put_int(C(0,1));   // 64
    puts_raw("C[1][0]="); put_int(C(1,0));   // 139
    puts_raw("C[1][1]="); put_int(C(1,1));   // 154

    // ---- 测试2:浮点矩阵(验证浮点路径)----
    // 用能精确表示的值:A=[[0.5,0.5]], B=[[2],[4]]
    // C = 0.5*2 + 0.5*4 = 1 + 2 = 3.0
    DenseMatrix<float> Af(1, 2);
    Af(0,0)=0.5f; Af(0,1)=0.5f;
    DenseMatrix<float> Bf(2, 1);
    Bf(0,0)=2.0f; Bf(1,0)=4.0f;
    DenseMatrix<float> Cf = matmul(Af, Bf);
    puts_raw("Cf[0][0]="); put_int((long)Cf(0,0));   // 3

    // ---- 测试3:稍大方阵,验证累加 + 拷贝/移动 ----
    // 5x5 全 1 矩阵自乘:结果每个元素 = 5
    int n = 5;
    DenseMatrix<int> O(n, n);
    for (int i=0;i<n;i++) for (int j=0;j<n;j++) O(i,j)=1;
    DenseMatrix<int> O2 = matmul(O, O);
    puts_raw("O2[2][3]="); put_int(O2(2,3));   // 5
    puts_raw("O2[4][4]="); put_int(O2(4,4));   // 5

    puts_raw("matmul test done\n");
    syscall3(0,0,0,0);
    return 0;
}