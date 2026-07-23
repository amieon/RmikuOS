// BenchMul.java —— 乘加 LCG
public class BenchMul {
    static final int SCALE = 0;
    static final int N = 10000000;

    public static void main(String[] args) {
        Rmiku.IO.printStr("[BENCH-BEGIN] mul_lcg");
        int n = N << SCALE;
        int x = 42;
        int acc = 0;
        for (int i = 0; i < n; i = i + 1) {
            x = x * 1103515245 + 12345;
            acc = acc + ((x >>> 16) & 0x7fff);
        }
        Rmiku.IO.printStr("iter=");
        Rmiku.IO.printInt(n);
        Rmiku.IO.printStr("checksum=");
        Rmiku.IO.printInt(acc);
        Rmiku.IO.printStr("[BENCH-END]");
    }
}