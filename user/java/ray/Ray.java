// ============================================================
// Ray.java —— RmikuRay 主控
//
// 流程：
//   1. spawn 两个 worker 线程（各自是一个独立的小 JVM）
//      上半幅 / 下半幅 并行渲染，结果写 /tmp/ray_bandN.txt
//   2. join 等两个线程结束
//   3. 读回两个 band 文件，拼帧输出完整画面
//
// 因为 RmikuOS 的线程各自拥有独立堆（对象不跨线程共享），
// 这里用文件系统做"进程间通信"——很 Unix，也很 OS。
// spawn 失败时退化为本进程串行渲染，保证画面一定能出来。
// ============================================================
public class Ray {
    public static void main(String[] args) {
        Rmiku.IO.printStr("================================================");
        Rmiku.IO.printStr("  RmikuRay - a ray tracer in pure Java");
        Rmiku.IO.printStr("  fixed-point 16.16 math, zero float instrs");
        Rmiku.IO.printStr("  2 threads -> band files -> assemble frame");
        Rmiku.IO.printStr("  phong | shadow | reflection | checkerboard");
        Rmiku.IO.printStr("================================================");

        int t0 = Rmiku.Thread.spawn("RayWorker", 0);
        int t1 = Rmiku.Thread.spawn("RayWorker", 1);

        if (t0 >= 0) {
            Rmiku.IO.printStr("worker 0 spawned, tid:");
            Rmiku.IO.printInt(t0);
        } else {
            Rmiku.IO.printStr("spawn 0 failed, render inline");
            RayWorker.run(0);
        }
        if (t1 >= 0) {
            Rmiku.IO.printStr("worker 1 spawned, tid:");
            Rmiku.IO.printInt(t1);
        } else {
            Rmiku.IO.printStr("spawn 1 failed, render inline");
            RayWorker.run(1);
        }

        if (t0 >= 0) {
            Rmiku.IO.printStr("worker 0 exit code:");
            Rmiku.IO.printInt(Rmiku.Thread.join(t0));
        }
        if (t1 >= 0) {
            Rmiku.IO.printStr("worker 1 exit code:");
            Rmiku.IO.printInt(Rmiku.Thread.join(t1));
        }

        Rmiku.IO.printStr("---------------- frame begin ----------------");

        String s0 = Rmiku.IO.readAll("/tmp/ray_band0.txt");
        if (s0 != null) {
            Rmiku.IO.printStr(s0);
        } else {
            Rmiku.IO.printStr("band0 file missing");
        }
        String s1 = Rmiku.IO.readAll("/tmp/ray_band1.txt");
        if (s1 != null) {
            Rmiku.IO.printStr(s1);
        } else {
            Rmiku.IO.printStr("band1 file missing");
        }

        Rmiku.IO.printStr("----------------- frame end -----------------");
        Rmiku.IO.printStr("scene: 3 spheres on checker plane, 1 sun");
        Rmiku.IO.printStr("resolution:");
        Rmiku.IO.printInt(100);
        Rmiku.IO.printStr("x");
        Rmiku.IO.printInt(40);
        Rmiku.IO.printStr("done.");
    }
}