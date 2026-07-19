#include "user.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: setip a.b.c.d\n");
        return 1;
    }
    unsigned int ip = parse_ip(argv[1]);
    if (!ip) {
        printf("bad ip: %s\n", argv[1]);
        return 1;
    }
    if (net_set_ip(ip) < 0) {
        printf("setip failed\n");
        return 1;
    }
    printf("MY_IP = %u.%u.%u.%u\n",
           (ip >> 24) & 0xff, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff);
    return 0;
}