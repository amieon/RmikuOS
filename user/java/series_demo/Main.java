public class Main {
    public static native void print(int n);

    public static void main(String[] args) {
        Series s = new Series();
        
        // 输出平方数：1 4 9 16 25
        for (int i = 1; i <= 5; i++) {
            print(s.square(i));
        }
        
        // 输出立方数：1 8 27 64 125
        for (int i = 1; i <= 5; i++) {
            print(s.cube(i));
        }
        
        // 输出金字塔数：1 22 333 4444 55555
        for (int i = 1; i <= 5; i++) {
            print(s.pyramid(i));
        }
    }
}