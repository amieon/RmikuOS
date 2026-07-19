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
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) { printf("[httpd] socket failed\n"); return 1; }

    struct sockaddr_in local = addr_of(0, HTTPD_PORT);
    if (bind(lfd, &local, sizeof local) < 0) { printf("[httpd] bind failed\n"); return 1; }
    if (listen(lfd, 4) < 0) { printf("[httpd] listen failed\n"); return 1; }
    printf("[httpd] RmikuOS httpd listening on 10.0.2.15:%d\n", HTTPD_PORT);

    static char req[REQ_BUF_SIZE];

    for (;;) {
        struct sockaddr_in from = {0};
        int cfd = accept(lfd, &from, 0);
        if (cfd < 0) {
            continue;                 // accept 周期性超时是正常的
        }
        unsigned ip = ntohl(from.sin_addr);
        printf("[httpd] conn from %u.%u.%u.%u:%u (fd=%d)\n",
               (ip>>24)&0xff, (ip>>16)&0xff, (ip>>8)&0xff, ip&0xff,
               ntohs(from.sin_port), cfd);

        int n = http_recv_request(cfd, req, sizeof(req));
        if (n <= 0) {
            uprintf("[httpd] idle conn fd=%d, kicked\n", cfd);
            net_close(cfd);
            continue;
        }
        /* 后面 parse/route/response 一字不动 */
        struct http_request r;
        if (http_parse_request(req, &r) < 0) { net_close(cfd); continue; }
        req_count++;
        printf("[httpd] #%d %s %s\n", req_count, r.method, r.path);
        

        struct route_result res = route_handle(r.path, req_count);
        if (res.code == 404 && http_try_file(cfd, r.path)) {
            printf("[httpd] #%d served file %s, fd=%d\n", req_count, r.path, cfd);
        } else {
            http_send_response(cfd, res.code, res.status, res.ctype, res.body, res.len);
            printf("[httpd] #%d served, %d bytes, closing fd=%d\n", req_count, res.len, cfd);
        }

        net_close(cfd);
    }
    return 0;
}