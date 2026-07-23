// BenchAlu.java —— 位运算混合
public class BenchAlu {
    static final int SCALE = 0;
    static final int N = 50000000; // 基准 5 千万，解释器约 5-10 秒

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
        Rmiku.IO.printStr("iter=" + n);
        Rmiku.IO.printStr("checksum=");
        Rmiku.IO.printInt(h);
        Rmiku.IO.printStr("[BENCH-END]");
    }
}