#include "files.h"

static int parse_int(const char *s) {
    int x = 0;
    int sign = 1;

    if (*s == '-') {
        sign = -1;
        s++;
    }

    while (*s >= '0' && *s <= '9') {
        x = x * 10 + (*s - '0');
        s++;
    }

    return x * sign;
}


static inline void put_int(long x) {
    char buf[32];
    int i = 0;

    if (x == 0) {
        put_char('0');
        return;
    }

    if (x < 0) {
        put_char('-');
        x = -x;
    }

    while (x > 0) {
        buf[i++] = '0' + (x % 10);
        x /= 10;
    }

    while (i > 0) {
        i--;
        put_char(buf[i]);
    }
}




