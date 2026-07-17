#pragma once

#define HTTPD_PORT   8080
#define REQ_BUF_SIZE 4096
#include "user.h"

struct http_request {
    char method[8];
    char path[128];
};

int  http_parse_request(const char *buf, struct http_request *req);
int  http_recv_request(int fd, char *buf, int cap);
int  http_send_all(int fd, const char *data, int len);
void http_send_response(int fd, int code, const char *status,
                        const char *ctype, const char *body, int body_len);