// 线程 demo：起两个线程各跑一个独立的小 JVM，主线程 join
public class ThreadDemo {
    public static void main(String[] args) {
        int t1 = Rmiku.Thread.spawn("Worker", 1);
        int t2 = Rmiku.Thread.spawn("Worker", 2);
        Rmiku.IO.printStr("spawned tids:");
        Rmiku.IO.printInt(t1);
        Rmiku.IO.printInt(t2);

        Rmiku.IO.printStr("thread1 exit:");
        Rmiku.IO.printInt(Rmiku.Thread.join(t1));
        Rmiku.IO.printStr("thread2 exit:");
        Rmiku.IO.printInt(Rmiku.Thread.join(t2));
        Rmiku.IO.printStr("all done");
    }
}
