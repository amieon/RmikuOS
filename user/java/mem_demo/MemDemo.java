// 手动内存 demo：Java 里用 malloc/free 管理一块原生内存
public class MemDemo {
    public static void main(String[] args) {
        // 分配 5 个 int 的空间
        int h = Rmiku.Mem.malloc(20);
        if (h < 0) {
            Rmiku.IO.printStr("malloc failed");
            return;
        }

        // 写入 10, 20, 30, 40, 50
        for (int i = 0; i < 5; i++) {
            Rmiku.Mem.store32(h, i * 4, (i + 1) * 10);
        }

        // 读回求和
        int sum = 0;
        for (int i = 0; i < 5; i++) {
            sum = sum + Rmiku.Mem.load32(h, i * 4);
        }
        Rmiku.IO.printStr("sum via native memory:");
        Rmiku.IO.printInt(sum);   // 150

        // 越界访问会被 native 拦下，返回 -1
        Rmiku.IO.printStr("out of bounds load32:");
        Rmiku.IO.printInt(Rmiku.Mem.load32(h, 100));

        Rmiku.Mem.free(h);
        Rmiku.IO.printStr("freed");
    }
}
