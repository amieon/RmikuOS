// 网络 demo：UDP 发包到 QEMU 网关（宿主机可用 nc -ul 9999 监听看内容）
public class NetDemo {
    public static void main(String[] args) {
        // QEMU user 模式网关固定是 10.0.2.2 = 0x0A000202
        int GW = 0x0A000202;

        int fd = Rmiku.Net.udpSocket();
        if (fd < 0) {
            Rmiku.IO.printStr("udp socket failed");
            return;
        }
        Rmiku.IO.printStr("udp socket fd:");
        Rmiku.IO.printInt(fd);

        int n = Rmiku.Net.sendTo(fd, "hello udp from jvm", GW, 9999);
        Rmiku.IO.printStr("sendTo returned:");
        Rmiku.IO.printInt(n);

        // 如果想收包：先 bind 再 recvFrom（阻塞）
        // Rmiku.Net.bind(fd, 9999);
        // byte[] buf = new byte[512];
        // int m = Rmiku.Net.recvFrom(fd, buf);
        // Rmiku.IO.printInt(m);
    }
}
