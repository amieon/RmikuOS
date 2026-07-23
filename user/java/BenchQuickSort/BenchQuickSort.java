// BenchQuickSort.java —— 快速排序（Hoare 分区，递归）
public class BenchQuickSort {
    static final int SCALE = 0;
    static final int N = 8192;
    static final int REP = 20;

    static void qsort(int[] a, int lo, int hi) {
        if (lo < hi) {
            int p = partition(a, lo, hi);
            qsort(a, lo, p);
            qsort(a, p + 1, hi);
        }
    }

    static int partition(int[] a, int lo, int hi) {
        int pivot = a[(lo + hi) >>> 1];
        int i = lo - 1;
        int j = hi + 1;
        while (true) {
            do { i = i + 1; } while (a[i] < pivot);
            do { j = j - 1; } while (a[j] > pivot);
            if (i >= j) return j;
            int t = a[i]; a[i] = a[j]; a[j] = t;
        }
    }

    public static void main(String[] args) {
        Rmiku.IO.printStr("[BENCH-BEGIN] quick_sort");
        int n = N;
        int rep = REP << SCALE;
        int[] a = new int[n];
        int x = 7;
        int acc = 0;
        for (int r = 0; r < rep; r = r + 1) {
            // 每轮重新随机化：避免有序数组的缓存/分支预测优势
            for (int i = 0; i < n; i = i + 1) {
                x = x * 1103515245 + 12345;
                a[i] = (x >>> 16) & 0x7fff;
            }
            qsort(a, 0, n - 1);
            acc = acc + a[0] + a[n - 1];
        }
        Rmiku.IO.printStr("len=" + n + " rep=" + rep);
        Rmiku.IO.printStr("checksum=");
        Rmiku.IO.printInt(acc);
        Rmiku.IO.printStr("[BENCH-END]");
    }
}