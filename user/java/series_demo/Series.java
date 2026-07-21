public class Series {
    public int square(int n) {
        return n * n;
    }
    
    public int cube(int n) {
        return n * n * n;
    }
    
    public int pyramid(int n) {
        int result = 0;
        for (int i = 0; i < n; i++) {
            result = result * 10 + n;
        }
        return result;
    }
}