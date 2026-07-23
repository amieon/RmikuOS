// BenchAlu.java —— 位运算混合
public class BenchAlu {
    static final int SCALE = 1;
    static final int N = 10000000;

    public static void main(String[] args) {
        Rmiku.IO.printStr("[BENCH-BEGIN] alu_mix");
        int n = N << SCALE;
        int h = 0x12345678;
        for (int i = 0; i < n; i = i + 1) {
            h ^= h << 13;
            h ^= h >>> 17;
            h ^= h << 5;
            h = h + i;
        }
        Rmiku.IO.printStr("iter=");
        Rmiku.IO.printInt(n);
        Rmiku.IO.printStr("checksum=");
        Rmiku.IO.printInt(h);
        Rmiku.IO.printStr("[BENCH-END]");
    }
}