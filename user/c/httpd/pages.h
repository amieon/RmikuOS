#pragma once
#include"user.h"

struct route_result {
    int code;
    const char *status;
    const char *ctype;
    const char *body;
    int len;
};

struct route_result route_handle(const char *path, int req_count);