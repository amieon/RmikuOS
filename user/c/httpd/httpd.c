#include "http.h"
#include "pages.h"

static int req_count = 0;
char http_file_buf[HTTP_FILE_CAP];
int  http_file_len = 0;

int main(int argc, char **argv)
{
    if (argc > 1) {
        int n = http_load_file(argv[1], http_file_buf, HTTP_FILE_CAP);
        if (n > 0) {
            http_file_len = n;
            uprintf("[httpd] loaded %s, %d bytes, serving at /\n", argv[1], n);
        } else {
            uprintf("[httpd] cannot open %s, fallback to inline pages\n", argv[1]);
        }
    }
    int lfd = net_socket_tcp();
    if (lfd < 0) { printf("[httpd] socket failed\n"); return 1; }
    if (net_bind(lfd, HTTPD_PORT) < 0) { printf("[httpd] bind failed\n"); return 1; }
    if (net_listen(lfd, 4) < 0) { printf("[httpd] listen failed\n"); return 1; }
    printf("[httpd] RmikuOS httpd listening on 10.0.2.15:%d\n", HTTPD_PORT);

    unsigned char info[8];
    static char req[REQ_BUF_SIZE];

    for (;;) {
        int cfd = net_accept(lfd, info);
        if (cfd < 0) {
            continue; // accept 周期性超时是正常的，继续等
        }
        printf("[httpd] conn from %d.%d.%d.%d:%d (fd=%d)\n",
               info[0], info[1], info[2], info[3],
               (info[4] << 8) | info[5], cfd);

        int n = http_recv_request(cfd, req, sizeof(req));
       
        if (n <= 0) {
            uprintf("[httpd] idle conn fd=%d, kicked\n", cfd);
            net_close(cfd);
            continue;                 // 真请求通常就在队列里排第二位
        }

        struct http_request r;
        if (http_parse_request(req, &r) < 0) { net_close(cfd); continue; }
        req_count++;
        printf("[httpd] #%d %s %s\n", req_count, r.method, r.path);

        struct route_result res = route_handle(r.path, req_count);
        http_send_response(cfd, res.code, res.status, res.ctype, res.body, res.len);
        // httpd.c，http_send_response 之后、net_close 之前加：
        printf("[httpd] #%d served, %d bytes, closing fd=%d\n", req_count, res.len, cfd);
        net_close(cfd);
    }
    return 0;
}