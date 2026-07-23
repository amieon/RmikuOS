// BenchArray.java —— 数组读写
public class BenchArray {
    static final int SCALE = 0;
    static final int N = 100000;
    static final int REP = 30;

    public static void main(String[] args) {
        Rmiku.IO.printStr("[BENCH-BEGIN] array_rw");
        int n = N;
        int rep = REP << SCALE;
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
        Rmiku.IO.printStr("len=");
        Rmiku.IO.printInt(n);
        Rmiku.IO.printStr(" rep=");
        Rmiku.IO.printInt(rep);
        Rmiku.IO.printStr(" checksum=");
        Rmiku.IO.printInt(acc);
        Rmiku.IO.printStr("[BENCH-END]");
    }
}