// BenchObject.java —— 对象分配与字段访问
public class BenchObject {
    static final int SCALE = 0;
    static final int N = 100000;

    static class Point {
        int x, y;
        Point(int x, int y) { this.x = x; this.y = y; }
        int getX() { return x; }
        int getY() { return y; }
    }

    public static void main(String[] args) {
        Rmiku.IO.printStr("[BENCH-BEGIN] object_field");
        int n = N << SCALE;
        int acc = 0;
        for (int i = 0; i < n; i = i + 1) {
            Point p = new Point(i, i * 3);
            acc = acc + p.getX() + p.getY();
        }
        Rmiku.IO.printStr("iter=");
        Rmiku.IO.printInt(n);
        Rmiku.IO.printStr("checksum=");
        Rmiku.IO.printInt(acc);
        Rmiku.IO.printStr("[BENCH-END]");
    }
}