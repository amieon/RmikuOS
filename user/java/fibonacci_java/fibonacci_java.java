public class fibonacci_java {
    public static native void print(int n);

    public static void main(String[] args) {
        int a = 0;
        int b = 1;
        // 输出斐波那契前 12 项：0 1 1 2 3 5 8 13 21 34 55 89
        for (int i = 0; i < 12; i++) {
            print(a);
            int c = a + b;
            a = b;
            b = c;
        }
        // 最后标记年份
        print(2026);
    }
}