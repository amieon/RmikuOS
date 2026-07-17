#include "pages.h"

static const char *PAGE_FMT =
"<!DOCTYPE html><html lang='zh'><head><meta charset='utf-8'>"
"<title>RmikuOS httpd</title><style>"
"body{background:#0d1117;color:#c9d1d9;font-family:monospace;max-width:720px;margin:60px auto;padding:0 20px}"
"h1{color:#58a6ff}.card{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:20px;margin:16px 0}"
".badge{color:#3fb950}a{color:#58a6ff}"
"</style></head><body>"
"<h1>RmikuOS <span class='badge'>httpd v1.0</span></h1>"
"<div class='card'>这个页面由 RmikuOS 自带的 TCP 协议栈实时提供。<br>"
"virtio-net &rarr; eth &rarr; arp &rarr; ip &rarr; tcp &rarr; socket syscall &rarr; 用户态 httpd,零第三方网络代码。</div>"
"<div class='card'><b>机器信息</b><br>"
"Arch: riscv64 / loongarch64 &nbsp; SMP: 8 harts<br>"
"IP: 10.0.2.15(DHCP 租约)&nbsp; TCP: 滑动窗口 + 超时重传 + 四次挥手<br>"
"<b>你看到的是本服务器处理的第 %d 个请求</b></div>"
"<div class='card'>试试: <a href='/hello'>/hello</a> &nbsp; "
"<a href='/api/stats'>/api/stats</a> &nbsp; <a href='/nope'>/nope(404)</a></div>"
"</body></html>";

static char page_buf[2048];
static char json_buf[256];

static struct route_result ok(const char *ctype, const char *body, int len) {
    struct route_result r = { 200, "OK", ctype, body, len };
    return r;
}

struct route_result route_handle(const char *path, int req_count) {
    if (strcmp(path, "/") == 0) {
        int n = snprintf(page_buf, sizeof(page_buf), PAGE_FMT, req_count);
        return ok("text/html; charset=utf-8", page_buf, n);
    }
    if (strcmp(path, "/hello") == 0) {
        const char *msg = "Hello from RmikuOS! 这一行字走过了完整的 TCP 三次握手。\n";
        int n = strlen(msg);
        return ok("text/plain; charset=utf-8", msg, n);
    }
    if (strcmp(path, "/api/stats") == 0) {
        int n = snprintf(json_buf, sizeof(json_buf),
            "{\"os\":\"RmikuOS\",\"requests\":%d,\"stack\":\"virtio-net/eth/arp/ip/tcp/dhcp\"}\n",
            req_count);
        return ok("application/json", json_buf, n);
    }
    {
        const char *msg = "404 Not Found —— RmikuOS httpd\n";
        struct route_result r = { 404, "Not Found", "text/plain; charset=utf-8", msg, strlen(msg) };
        return r;
    }
}