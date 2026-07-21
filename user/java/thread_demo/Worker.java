// 线程入口类：必须是 public static void run(int arg)
public class Worker {
    public static void run(int id) {
        for (int i = 0; i < 5; i++) {
            Rmiku.IO.printInt(id * 100 + i);
            Rmiku.Proc.yield();   // 让出 CPU，看到交错输出
        }
    }
}
