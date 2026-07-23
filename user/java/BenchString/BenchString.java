// BenchString.java —— 字符串常量加载（ldc String）
public class BenchString {
    static final int SCALE = 0;
    static final int N = 1000000;

    public static void main(String[] args) {
        Rmiku.IO.printStr("[BENCH-BEGIN] string_ldc");
        int n = N << SCALE;
        int acc = 0;
        for (int i = 0; i < n; i = i + 1) {
            String s = "hello";
            if (s != null) acc = acc + 1;
            String t = "world";
            if (t != null) acc = acc + 2;
        }
        Rmiku.IO.printStr("iter=");
        Rmiku.IO.printInt(n);
        Rmiku.IO.printStr("checksum=");
        Rmiku.IO.printInt(acc);
        Rmiku.IO.printStr("[BENCH-END]");
    }
}