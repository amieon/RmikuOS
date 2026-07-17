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
int http_send_all(int fd, const char *buf, int len)
{
    int sent = 0;
    while (sent < len) {
        int chunk = (len - sent > 1400) ? 1400 : len - sent;
        int n = net_send(fd, buf + sent, chunk);
       // uprintf("[http] send %d..%d -> %d\n", sent, sent + chunk, n);
        if (n <= 0) {
            uprintf("[http] STALLED at %d/%d\n", sent, len);
            return sent;
        }
        sent += n;
    }
    return sent;
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

// 把整个文件读进 buf,返回字节数;失败返回 -1。
// 适配点就这三个:open/read/close,名字和参数按你的 fs 系统调用改。
int http_load_file(const char *path, char *buf, int cap)
{
    int fd = open(path, 0);              // 0 = 只读
    if (fd < 0)
        return -1;
    int used = 0, n;
    while (used < cap - 1 && (n = read(fd, buf + used, cap - 1 - used)) > 0)
        used += n;
    close(fd);
    buf[used] = 0;
    return used;
}

// 发文件内容(显式长度,不靠 strlen,以后发图片等二进制也不用改)
void http_send_file(int fd, const char *body, int len)
{
    char hdr[128];
    int hn = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n", len);
    http_send_all(fd, hdr, hn);
    http_send_all(fd, body, len);        // 1400 切片逻辑复用,不用动
}