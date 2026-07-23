// Bench.java —— RmikuOS 自研 JVM 的 AOT vs 解释器 冒烟基准
//
// 五个纯整数内核，各自带校验和：
//   K1 位运算混合循环   K2 乘加 LCG      K3 数组读写
//   K4 静态调用 + 递归  K5 冒泡排序（分支密集）
//
// 用法：
//   tick jvm Bench.class          # AOT 开
//   （关掉 AOT 重编 jvm 后再跑一次）
//   两次输出的 checksum 必须完全一致——不一致说明 AOT 算错了，不是快慢问题
//
// 太慢：把下面 SCALE 改成 1（再慢就 0，即 1/4 规模）；太快：改成 4。

public class Bench {

    // 全局规模旋钮：实际迭代数 = 基准值 << SCALE
    static final int SCALE = 1;

    static final int N_ALU   = 50000000;    // K1 基准迭代
    static final int N_MUL   = 50000000;    // K2 基准迭代
    static final int N_ARR   = 100000;      // K3 数组长度
    static final int REP_ARR = 200;         // K3 重复轮数
    static final int N_CALL  = 100000;      // K4 静态调用次数
    static final int FIB_N   = 8;           // K4b 递归规模
    static final int FIB_REP = 16;          // K4b 重复轮数
    static final int N_SORT  = 2048;        // K5 数组长度
    static final int REP_SORT = 10;         // K5 重复轮数

    public static void main(String[] args) {
        Rmiku.IO.printStr("[BENCH-BEGIN]");

        kAlu();
        kMul();
        kArr();
        kCall();
        kSort();

        Rmiku.IO.printStr("[BENCH-END]");
    }

    // ---- K1：位运算混合（ishl/iushr/ixor/iadd + 循环分支）----
    static void kAlu() {
        Rmiku.IO.printStr("[K1-BEGIN] alu-mix");
        int n = N_ALU << SCALE;
        int h = 0x12345678;
        for (int i = 0; i < n; i = i + 1) {
            h ^= h << 13;
            h ^= h >>> 17;
            h ^= h << 5;
            h = h + i;
        }
        Rmiku.IO.printStr("[K1-END] checksum=");
        Rmiku.IO.printInt(h);
    }

    // ---- K2：乘加 LCG（imul/iadd 常数乘）----
    static void kMul() {
        Rmiku.IO.printStr("[K2-BEGIN] mul-add lcg");
        int n = N_MUL << SCALE;
        int x = 42;
        int acc = 0;
        for (int i = 0; i < n; i = i + 1) {
            x = x * 1103515245 + 12345;
            acc = acc + ((x >>> 16) & 0x7fff);
        }
        Rmiku.IO.printStr("[K2-END] checksum=");
        Rmiku.IO.printInt(acc);
    }

    // ---- K3：数组读写（newarray/iastore/iaload/arraylength）----
    static void kArr() {
        Rmiku.IO.printStr("[K3-BEGIN] array rw");
        int n = N_ARR;
        int rep = REP_ARR << SCALE;
        int[] a = new int[n];
        int acc = 0;
        for (int r = 0; r < rep; r = r + 1) {
            for (int i = 0; i < a.length; i = i + 1) {
                a[i] = (i * 7 + r) & 0xffff;
            }
            for (int i = a.length - 1; i >= 0; i = i - 1) {
                acc = acc + a[i];
            }
        }
        Rmiku.IO.printStr("[K3-END] checksum=");
        Rmiku.IO.printInt(acc);
    }

    // ---- K4：静态调用（invokestatic）+ 递归 ----
    static int addmul(int a, int b, int c) {
        return a * b + c;
    }

    static int fib(int n) {
        if (n < 2) {
            return n;
        }
        return fib(n - 1) + fib(n - 2);
    }

    static void kCall() {
        Rmiku.IO.printStr("[K4-BEGIN] static call + recursion");
        int n = N_CALL << SCALE;
        int acc = 0;
        for (int i = 0; i < n; i = i + 1) {
            acc = addmul(acc, i & 0xff, 3);
        }
        int fr = FIB_REP << SCALE;
        int f = 0;
        for (int r = 0; r < fr; r = r + 1) {
            f = f + fib(FIB_N);
        }
        Rmiku.IO.printStr("[K4-END] checksum=");
        Rmiku.IO.printInt(acc + f);
    }

    // ---- K5：冒泡排序（比较分支密集 + 数组交换）----
    static void kSort() {
        Rmiku.IO.printStr("[K5-BEGIN] bubble sort");
        int n = N_SORT;
        int rep = REP_SORT << SCALE;
        int[] a = new int[n];
        int x = 7;
        int acc = 0;
        for (int r = 0; r < rep; r = r + 1) {
            for (int i = 0; i < n; i = i + 1) {
                x = x * 1103515245 + 12345;
                a[i] = (x >>> 16) & 0x7fff;
            }
            for (int i = 0; i < n - 1; i = i + 1) {
                for (int j = 0; j < n - 1 - i; j = j + 1) {
                    if (a[j] > a[j + 1]) {
                        int t = a[j];
                        a[j] = a[j + 1];
                        a[j + 1] = t;
                    }
                }
            }
            acc = acc + a[0] + a[n - 1];
        }
        Rmiku.IO.printStr("[K5-END] checksum=");
        Rmiku.IO.printInt(acc);
    }
}