/* wget.c —— 迷你 HTTP 下载工具（RmikuOS）
 *
 * 用 RmikuOS 的 TCP 栈发 HTTP/1.0 GET, 把响应体存到本地文件。
 *
 * 用法: wget <url> [outfile]                 URL 形式(host 支持 IP 或域名)
 *   例:  wget http://10.0.2.2:8000/hello.txt
 *        wget http://10.0.2.2/hello.txt      默认端口 80
 *        wget http://10.0.2.2:8000/a.txt /fat/a.txt
 *       wget <ip> <port> <path> [outfile]    旧三参形式(兼容)
 *
 * 流程: socket -> connect -> send GET -> recv 响应头(\r\n\r\n 分隔) -> recv body
 *       (HTTP/1.0 + Connection: close, 服务器关闭连接即收完) -> 写文件
 */
#include "user.h"

#define BUF_SIZE 8192

/* 解析 http://host[:port]/path -> host/port/path。host 支持 IP 或域名。 */
static int parse_url(const char *url, char *ip, int *port, char *path) {
    if (strncmp(url, "http://", 7) != 0) return 0;
    const char *p = url + 7;
    int i = 0;
    while (*p && *p != ':' && *p != '/') {
        if (i < 63) ip[i++] = *p;
        p++;
    }
    ip[i] = '\0';
    if (i == 0) return 0;                    /* 没 host */
    *port = 80;                              /* 默认端口 */
    if (*p == ':') {
        p++;
        *port = 0;
        while (*p >= '0' && *p <= '9') {
            *port = *port * 10 + (*p - '0');
            p++;
        }
    }
    if (*p == '/') strcpy(path, p);
    else           strcpy(path, "/");        /* 无路径 -> 根 */
    return 1;
}

int main(int argc, char **argv) {
    const char *ip, *path, *out;
    int port;
    static char ip_buf[64], path_buf[256];

    if (argc >= 2 && strncmp(argv[1], "http://", 7) == 0) {
        /* URL 形式 */
        if (!parse_url(argv[1], ip_buf, &port, path_buf)) {
            printf("wget: bad url: %s\n", argv[1]);
            return 1;
        }
        ip = ip_buf;
        path = path_buf;
        out = argc >= 3 ? argv[2] : "/fat/wget.out";
    } else if (argc >= 4) {
        /* 旧三参形式 */
        ip   = argv[1];
        port = atoi(argv[2]);
        path = argv[3];
        out  = argc >= 5 ? argv[4] : "/fat/wget.out";
    } else {
        printf("usage: wget <url> [outfile]\n"
               "       wget <ip> <port> <path> [outfile]\n");
        return 1;
    }

    /* ---- 解析 host:先当点分 IP,失败则域名解析 ---- */
    unsigned int host_ip = parse_ip(ip);
    if (host_ip == 0) host_ip = net_resolve(ip);
    if (host_ip == 0) {
        printf("wget: cannot resolve %s\n", ip);
        return 1;
    }

    /* ---- 建连 ---- */
    int fd = socket_tcp();
    if (fd < 0) { printf("wget: socket failed\n"); return 1; }

    /* addr_of 内部已 htons(port) —— 不要再套 htons, 否则双重转换变回主机序 */
    struct sockaddr_in srv = addr_of(host_ip, (unsigned short)port);
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

    /* ---- 收响应: 一次读大块(内核 recv 弹出整个 TCP chunk, 只返回请求长度——
     * 逐字节读会把整块数据弹掉丢弃!), 缓冲里找 \r\n\r\n 分隔头与体 ---- */
    static char rbuf[BUF_SIZE];
    int used = 0;
    int hdr_end = -1;
    while (used < (int)sizeof rbuf - 1) {
        int n = recv(fd, rbuf + used, (int)sizeof rbuf - 1 - used, 0);
        if (n <= 0) {
            printf("wget: connection closed before headers\n");
            return 1;
        }
        used += n;
        for (int i = 3; i < used; i++) {
            if (rbuf[i-3] == '\r' && rbuf[i-2] == '\n' &&
                rbuf[i-1] == '\r' && rbuf[i] == '\n') {
                hdr_end = i + 1;
                break;
            }
        }
        if (hdr_end >= 0) break;
    }
    if (hdr_end < 0) {
        printf("wget: headers too long\n");
        return 1;
    }
    rbuf[hdr_end - 4] = '\0';          /* 状态行截断(分隔前) */
    printf("wget: %s\n", rbuf);        /* 打印状态行如 "HTTP/1.0 200 OK" */
    if (strncmp(rbuf, "HTTP/1.0 4", 10) == 0 || strncmp(rbuf, "HTTP/1.0 5", 10) == 0) {
        printf("wget: server error, abort\n");
        return 1;
    }

    /* ---- 写文件: 先把缓冲里已收的 body 部分写出, 再继续 recv ---- */
    int outfd = open(out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (outfd < 0) {
        printf("wget: cannot open %s\n", out);
        return 1;
    }

    long total = 0;
    if (used > hdr_end) {
        if (write(outfd, rbuf + hdr_end, used - hdr_end) != used - hdr_end) {
            printf("wget: write failed\n");
            return 1;
        }
        total += used - hdr_end;
    }
    for (;;) {
        int n = recv(fd, rbuf, sizeof rbuf, 0);
        if (n <= 0) break;                  /* 连接关闭 = body 完 */
        if (write(outfd, rbuf, n) != n) {
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
