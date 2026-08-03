/* wget.c —— 迷你 HTTP 下载工具（RmikuOS）
 *
 * 用 RmikuOS 的 TCP 栈发 HTTP/1.0 GET, 把响应体存到本地文件。
 *
 * 用法: wget <ip> <port> <path> [outfile]
 *   例:  wget 10.0.2.2 8000 /hello.txt           -> 存 /fat/wget.out
 *        wget 10.0.2.2 8000 /hello.txt /fat/hi.txt
 *
 * 流程: socket -> connect -> send GET -> recv 响应头(\r\n\r\n 分隔) -> recv body
 *       (HTTP/1.0 + Connection: close, 服务器关闭连接即收完) -> 写文件
 */
#include "user.h"

#define BUF_SIZE 8192

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("usage: wget <ip> <port> <path> [outfile]\n");
        return 1;
    }
    const char *ip   = argv[1];
    int port         = atoi(argv[2]);
    const char *path = argv[3];
    const char *out  = argc >= 5 ? argv[4] : "/fat/wget.out";

    /* ---- 建连 ---- */
    int fd = socket_tcp();
    if (fd < 0) { printf("wget: socket failed\n"); return 1; }

    struct sockaddr_in srv = addr_of(parse_ip(ip), htons((unsigned short)port));
    if (connect(fd, &srv, sizeof srv) < 0) {
        printf("wget: connect %s:%d failed\n", ip, port);
        return 1;
    }

    /* ---- 发 GET 请求(HTTP/1.0, Connection: close) ---- */
    char req[512];
    int rlen = snprintf(req, sizeof req,
                        "GET %s HTTP/1.0\r\n"
                        "Host: %s:%d\r\n"
                        "Connection: close\r\n"
                        "\r\n",
                        path, ip, port);
    if (send(fd, req, rlen, 0) < 0) {
        printf("wget: send failed\n");
        return 1;
    }

    /* ---- 收响应头(找 \r\n\r\n) ---- */
    static char hdr[BUF_SIZE];
    int hlen = 0;
    while (hlen < (int)sizeof hdr - 1) {
        int n = recv(fd, hdr + hlen, 1, 0);   /* 逐字节找分隔(教学简单) */
        if (n <= 0) { printf("wget: connection closed before headers\n"); return 1; }
        hlen++;
        if (hlen >= 4 &&
            hdr[hlen-4] == '\r' && hdr[hlen-3] == '\n' &&
            hdr[hlen-2] == '\r' && hdr[hlen-1] == '\n')
            break;
    }
    hdr[hlen] = '\0';

    /* 打印状态行 */
    printf("wget: %s", hdr);   /* 状态行如 "HTTP/1.0 200 OK" */
    if (strstr(hdr, " 404 ") || strncmp(hdr, "HTTP/1.0 4", 10) == 0 ||
        strncmp(hdr, "HTTP/1.0 5", 10) == 0) {
        printf("wget: server error, abort\n");
        return 1;
    }

    /* ---- 收 body 写到文件 ---- */
    int outfd = open(out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (outfd < 0) {
        printf("wget: cannot open %s\n", out);
        return 1;
    }

    long total = 0;
    static char buf[BUF_SIZE];
    for (;;) {
        int n = recv(fd, buf, sizeof buf, 0);
        if (n <= 0) break;                  /* 连接关闭 = body 完 */
        if (write(outfd, buf, n) != n) {
            printf("wget: write failed\n");
            return 1;
        }
        total += n;
    }

    close(outfd);
    close(fd);
    printf("wget: saved %ld bytes -> %s\n", total, out);
    return 0;
}
