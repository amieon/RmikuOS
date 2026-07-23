// BenchBranch.java —— 分支预测密集
public class BenchBranch {
    static final int SCALE = 0;
    static final int N = 10000000;

    public static void main(String[] args) {
        Rmiku.IO.printStr("[BENCH-BEGIN] branch_heavy");
        int n = N << SCALE;
        int acc = 0;
        for (int i = 0; i < n; i = i + 1) {
            int v = i & 0xf;
            if (v == 0) acc = acc + 1;
            else if (v == 1) acc = acc + 2;
            else if (v == 2) acc = acc + 3;
            else if (v == 3) acc = acc + 4;
            else if (v == 4) acc = acc + 5;
            else if (v == 5) acc = acc + 6;
            else if (v == 6) acc = acc + 7;
            else if (v == 7) acc = acc + 8;
            else if (v == 8) acc = acc + 9;
            else if (v == 9) acc = acc + 10;
            else if (v == 10) acc = acc + 11;
            else if (v == 11) acc = acc + 12;
            else if (v == 12) acc = acc + 13;
            else if (v == 13) acc = acc + 14;
            else if (v == 14) acc = acc + 15;
            else acc = acc + 16;
        }
        Rmiku.IO.printStr("iter=");
        Rmiku.IO.printInt(n);
        Rmiku.IO.printStr("checksum=");
        Rmiku.IO.printInt(acc);
        Rmiku.IO.printStr("[BENCH-END]");
    }
}