// 文件读写 demo：整写整读 + fd 级别的 read 到 byte[]
public class FileDemo {
    public static void main(String[] args) {
        // 1. 一把梭：writeAll / readAll
        int w = Rmiku.IO.writeAll("/tmp/from_java.txt", "hello file, from JVM");
        Rmiku.IO.printStr("writeAll returned:");
        Rmiku.IO.printInt(w);

        String s = Rmiku.IO.readAll("/tmp/from_java.txt");
        if (s == null) {
            Rmiku.IO.printStr("readAll failed");
            return;
        }
        Rmiku.IO.printStr(s);

        // 2. fd 级别：open -> read 到 byte[] -> 逐字节打印
        int fd = Rmiku.IO.open("/tmp/from_java.txt", Rmiku.IO.O_RDONLY);
        if (fd < 0) {
            Rmiku.IO.printStr("open failed");
            return;
        }
        byte[] buf = new byte[128];
        int n = Rmiku.IO.read(fd, buf);
        Rmiku.IO.printStr("fd read bytes:");
        Rmiku.IO.printInt(n);
        for (int i = 0; i < n; i++) {
            Rmiku.IO.printInt(buf[i]);   // 打印每个字节的 ASCII 值
        }
        Rmiku.IO.close(fd);
    }
}
