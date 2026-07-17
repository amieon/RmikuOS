#include "pages.h"
#include "http.h"
#include "string.h"

static char page_buf[2048];
static char json_buf[256];

static struct route_result ok(const char *ctype, const char *body, int len) {
    struct route_result r = { 200, "OK", ctype, body, len };
    return r;
}

// 内联演示页(原 GitHub 风首页,现住 /demo;%d = 请求计数)
static const char PAGE_FMT[] =
    "<!DOCTYPE html><html lang='zh-CN'><head><meta charset='UTF-8'>"
    "<title>RmikuOS httpd</title><style>"
    "body{background:#0d1117;color:#e6edf3;font-family:system-ui,sans-serif;"
    "display:flex;justify-content:center;align-items:center;min-height:100vh;margin:0}"
    ".box{text-align:center;max-width:640px;padding:0 24px}"
    "h1{background:linear-gradient(90deg,#22d3ee,#a78bfa);-webkit-background-clip:text;"
    "background-clip:text;color:transparent;font-size:56px;margin:0 0 16px}"
    ".tag{color:#8b98a9;font-size:14px}a{color:#58a6ff}"
    "</style></head><body><div class='box'>"
    "<h1>RmikuOS</h1>"
    "<p>这个内联页面由 RmikuOS 自研 TCP/IP 协议栈 + 用户态 httpd 提供。</p>"
    "<p class='tag'>你看到的是本服务器处理的第 %d 个请求 &middot; <a href='/'>返回 wow 页</a></p>"
    "</div></body></html>";

struct route_result route_handle(const char *path, int req_count) {
    // 1. 文件模式:/ 与 /index.html 发启动时已读进内存的文件。
    //    注意:文件在 main() 里就加载好了,这里只引用,不 open、不 send。
    if (http_file_len > 0 &&
        (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0))
        return ok("text/html; charset=utf-8", http_file_buf, http_file_len);

    // 2. 内联首页:无文件模式时的 /;有文件模式时走 /demo
    if (strcmp(path, "/") == 0 || strcmp(path, "/demo") == 0) {
        int n = snprintf(page_buf, sizeof(page_buf), PAGE_FMT, req_count);
        if (n >= (int)sizeof(page_buf))
            n = sizeof(page_buf) - 1;    // snprintf 返回"本应写入长度",截断要钳位
        return ok("text/html; charset=utf-8", page_buf, n);
    }

    // 3. /hello
    if (strcmp(path, "/hello") == 0) {
        const char *msg = "Hello from RmikuOS httpd!\n";
        return ok("text/plain; charset=utf-8", msg, strlen(msg));
    }

    // 4. /api/stats:wow.html 每 2s 轮询,JS 只认 "requests" 这个字段
    if (strcmp(path, "/api/stats") == 0) {
        int n = snprintf(json_buf, sizeof(json_buf),
            "{\"requests\":%d,\"os\":\"RmikuOS\",\"stack\":\"self-made\"}",
            req_count);
        if (n >= (int)sizeof(json_buf))
            n = sizeof(json_buf) - 1;
        return ok("application/json", json_buf, n);
    }

    // 5. 404
    {
        const char *msg = "404 Not Found —— RmikuOS httpd\n";
        struct route_result r = { 404, "Not Found", "text/plain; charset=utf-8", msg, strlen(msg) };
        return r;
    }
}