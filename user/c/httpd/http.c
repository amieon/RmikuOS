#include "http.h"

/* 解析 "GET /path HTTP/1.1\r\n" 请求行，头部其余字段一律忽略 */
int http_parse_request(const char *buf, struct http_request *req) {
    int i = 0, j = 0;
    while (buf[i] && buf[i] != ' ' && j < 7) req->method[j++] = buf[i++];
    req->method[j] = 0;
    if (buf[i] != ' ') return -1;
    i++;
    j = 0;
    while (buf[i] && buf[i] != ' ' && buf[i] != '\r' && j < 127) req->path[j++] = buf[i++];
    req->path[j] = 0;
    return 0;
}

/* 收直到 \r\n\r\n（头结束）或 EOF。TCP 是流，必须自己找边界 */
// 收一个 HTTP 请求头。返回 >0:请求字节数;<=0:空连接/出错,调用方直接 close。
int http_recv_request(int fd, char *buf, int cap)
{
    int used = 0;
    while (used < cap - 1) {
        int n = net_recv(fd, buf + used, cap - 1 - used);
        if (n <= 0)
            break;                    // 0=超时/EOF,-1=RST:都不值得等
        used += n;
        buf[used] = 0;
        if (strstr(buf, "\r\n\r\n"))  // 找到头部边界
            break;
    }
    buf[used] = 0;
    return used;
}

/* 内核 sys_net_send 单次上限 1460，大了要切片 */
int http_send_all(int fd, const char *data, int len) {
    int off = 0;
    while (off < len) {
        int chunk = len - off;
        if (chunk > 1400) chunk = 1400;
        int n = net_send(fd, data + off, chunk);
        if (n <= 0) return -1;
        off += n;
    }
    return 0;
}

void http_send_response(int fd, int code, const char *status,
                        const char *ctype, const char *body, int body_len) {
    char hdr[256];
    int hlen = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n", code, status, ctype, body_len);
    http_send_all(fd, hdr, hlen);
    if (body_len > 0)
        http_send_all(fd, body, body_len);
}