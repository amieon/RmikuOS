/* halfclose.c —— SYS_NET_SHUTDOWN(114) 半关闭验证程序
 *
 * 场景:A(guest)向 B(宿主机)发一段"长度未知"的数据,用 shutdown(SHUT_WR)
 * 宣告"我说完了",然后继续收 B 回传的、同样长度未知的结果,直到 EOF。
 *
 * 配套宿主机端: python3 halfclose_server.py  (默认 9999 端口)
 *
 * 用法: halfclose [ip] [port]        默认 10.0.2.2 9999
 *
 * 检查点(全部 PASS 则半关闭链路正确):
 *   [1] SHUT_WR 后 send 必须失败(-1)        —— 写方向确实关了
 *   [2] SHUT_WR 后 recv 照常工作            —— 读方向还活着
 *   [3] 收到对端 FIN 时 recv 返回 0(EOF)    —— 挥手后半完整
 *   [4] 回传字节数与请求一致                —— 数据没丢
 */
#include "user.h"

#define BUF_SIZE 1024

/* 造一段"长度未知"的负载:行数由 tick 决定,服务端事先无法预知 */
static int make_request(char *buf, int cap) {
    int lines = 3 + (int)(get_ticks() % 5);      /* 3~7 行 */
    int n = 0;
    for (int i = 0; i < lines && n < cap - 32; i++)
        n += snprintf(buf + n, cap - n, "line %d from RmikuOS\n", i);
    return n;
}

int main(int argc, char **argv) {
    unsigned int ip = argc > 1 ? parse_ip(argv[1]) : 0x0A000202; /* 10.0.2.2 */
    int port        = argc > 2 ? atoi(argv[2]) : 9999;
    if (ip == 0) { printf("halfclose: bad ip\n"); return 1; }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { printf("[1] socket failed\n"); return 1; }

    struct sockaddr_in srv = addr_of(ip, port);
    if (connect(fd, &srv, sizeof srv) < 0) {
        printf("halfclose: connect %d.%d.%d.%d:%d failed (server up?)\n",
               (ip>>24)&0xff, (ip>>16)&0xff, (ip>>8)&0xff, ip&0xff, port);
        net_close(fd);
        return 1;
    }
    printf("connected, fd=%d\n", fd);

    /* ---- ① 发送"长度未知"的请求 ---- */
    static char req[BUF_SIZE];
    int req_len = make_request(req, sizeof req);
    int sent = 0;
    while (sent < req_len) {
        int n = send(fd, req + sent, req_len - sent, 0);
        if (n <= 0) { printf("send stalled at %d/%d\n", sent, req_len); net_close(fd); return 1; }
        sent += n;
    }
    printf("request sent: %d bytes (server 不知道有多长)\n", sent);

    /* ---- ② 半关闭:宣布"我说完了",听筒不摘 ---- */
    if (net_shutdown(fd, SHUT_WR) < 0) {
        printf("FAIL: net_shutdown(SHUT_WR) returned -1\n");
        net_close(fd);
        return 1;
    }
    printf("shutdown(SHUT_WR) ok —— FIN 已发出,fd 还活着\n");

    /* [1] 写方向必须已死 */
    int x = send(fd, "ghost", 5, 0);
    printf("[%s] send after SHUT_WR -> %d (期望 -1)\n",
           x < 0 ? "PASS" : "FAIL", x);

    /* ---- ③ 收结果:长度同样未知,收到对端 FIN 为止 ---- */
    static char buf[BUF_SIZE];
    int total = 0, eof_ok = 0;
    for (;;) {
        int n = recv(fd, buf, sizeof buf, 0);
        if (n < 0) { printf("recv error\n"); break; }
        if (n == 0) { eof_ok = 1; break; }   /* 对端 FIN = 结果结束 */
        total += n;
    }
    printf("[%s] recv after SHUT_WR worked, got %d bytes\n",
           total > 0 ? "PASS" : "FAIL", total);
    printf("[%s] peer FIN observed as EOF (recv -> 0)\n",
           eof_ok ? "PASS" : "FAIL");
    printf("[%s] echo size matches request (%d == %d)\n",
           total == sent ? "PASS" : "WARN", total, sent);

    close(fd);   /* 统一 fd 表:普通 close 也能关 socket,顺便验证 10 号路径 */
    printf("closed via sys_close(10)\n");
    return 0;
}