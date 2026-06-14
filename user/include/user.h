#include "files.h"
#include "lock.h"
#include "thread.h"
#include "uprintf.h"

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

static int append_str(char *buf, int pos, const char *s) {
    while (*s) {
        buf[pos++] = *s++;
    }
    return pos;
}

static int append_int(char *buf, int pos, int x) {
    char tmp[16];
    int n = 0;

    if (x == 0) {
        buf[pos++] = '0';
        return pos;
    }

    if (x < 0) {
        buf[pos++] = '-';
        x = -x;
    }

    while (x > 0) {
        tmp[n++] = '0' + (x % 10);
        x /= 10;
    }

    while (n > 0) {
        buf[pos++] = tmp[--n];
    }

    return pos;
}

char c(long x){
    if(x <= 9)return x+'0';
    return x-10+'a';
}

static inline void put_hex(long x) {
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
        buf[i++] = c(x % 16);
        x /= 16;
    }

    while (i > 0) {
        i--;
        put_char(buf[i]);
    }
}

static int append_usize(char *buf, int pos, usize x) {
    char tmp[32];
    int n = 0;

    if (x == 0) {
        buf[pos++] = '0';
        return pos;
    }

    while (x > 0) {
        tmp[n++] = '0' + (x % 10);
        x /= 10;
    }

    while (n > 0) {
        buf[pos++] = tmp[--n];
    }

    return pos;
}