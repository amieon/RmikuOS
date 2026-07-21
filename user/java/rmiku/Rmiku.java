/**
 * Rmiku.java —— RmikuOS JVM 的系统 API 库
 *
 * 所有方法都是 native，由 user/cpp/jvm/native.cpp 实现。
 * 用法：把本文件和你的 demo 一起编译，Rmiku*.class 会生成到同一目录：
 *   javac -d <demo_dir> rmiku/Rmiku.java <demo_dir>/Xxx.java
 */
public final class Rmiku {

    /** 文件 + 控制台输入输出 */
    public static final class IO {
        // 与 user/include/flag.h 对齐
        public static final int O_RDONLY = 0;
        public static final int O_WRONLY = 1;
        public static final int O_RDWR   = 2;
        public static final int O_CREAT  = 0x40;
        public static final int O_TRUNC  = 0x200;
        public static final int O_APPEND = 0x400;

        public static native void printInt(int v);
        public static native void printStr(String s);   // 带换行

        public static native int open(String path, int flags);
        public static native int create(String path);   // WRONLY|CREAT|TRUNC
        public static native int close(int fd);
        public static native int read(int fd, byte[] buf);
        public static native int write(int fd, byte[] buf, int len);
        public static native int writeStr(int fd, String s);

        public static native String readAll(String path);              // 失败返回 null
        public static native int writeAll(String path, String content);

        public static native int readChar();            // 控制台读一个字符，EOF 返回 -1
        public static native String readLine();         // 控制台读一行（不含 \n）
    }

    /** 手动内存管理：Java 里玩 malloc/free */
    public static final class Mem {
        public static native int malloc(int size);      // 返回句柄(>0)，失败 -1
        public static native void free(int handle);
        public static native int load8(int handle, int off);
        public static native void store8(int handle, int off, int v);
        public static native int load32(int handle, int off);   // 小端
        public static native void store32(int handle, int off, int v);
    }

    /** 进程 */
    public static final class Proc {
        public static native int fork();
        public static native int waitpid(int pid);      // 返回子进程退出码，失败 -1
        public static native int getpid();
        public static native void sleep(int ticks);
        public static native void yield();
        public static native void exit(int code);
    }

    /**
     * 用户态线程。
     * spawn 会在新线程里启动一个独立的小 JVM，加载指定类并执行
     *   public static void run(int arg)
     * 每个线程有独立的堆和 GC，对象不跨线程共享。
     */
    public static final class Thread {
        public static native int spawn(String className, int arg);  // 返回 tid，失败 -1
        public static native int join(int tid);                     // 返回线程退出码
    }

    /**
     * 网络。ip 用 int 主机序表示：10.0.2.2 = 0x0A000202
     */
    public static final class Net {
        public static native int udpSocket();
        public static native int tcpSocket();
        public static native int bind(int fd, int port);
        public static native int connect(int fd, int ip, int port);
        public static native int send(int fd, String data);
        public static native int sendTo(int fd, String data, int ip, int port);
        public static native int recv(int fd, byte[] buf);
        public static native int recvFrom(int fd, byte[] buf);
    }
}
