// BenchCall.java —— 静态调用 + 递归
public class BenchCall {
    static final int SCALE = 0;
    static final int N = 1000000;
    static final int FIB_N = 8;
    static final int FIB_REP = 1024;

    static int addmul(int a, int b, int c) {
        return a * b + c;
    }

    static int fib(int n) {
        if (n < 2) return n;
        return fib(n - 1) + fib(n - 2);
    }

    public static void main(String[] args) {
        Rmiku.IO.printStr("[BENCH-BEGIN] static_call");
        int n = N << SCALE;
        int acc = 0;
        for (int i = 0; i < n; i = i + 1) {
            acc = addmul(acc, i & 0xff, 3);
        }
        int fr = FIB_REP << SCALE;
        int f = 0;
        for (int r = 0; r < fr; r = r + 1) {
            f = f + fib(FIB_N);
        }
        Rmiku.IO.printStr("call_iter=");
        Rmiku.IO.printInt(n);
        Rmiku.IO.printStr(" fib_rep=");
        Rmiku.IO.printInt(fr);
        Rmiku.IO.printStr(" checksum=");
        Rmiku.IO.printInt(acc + f);
        Rmiku.IO.printStr("[BENCH-END]");
    }
}