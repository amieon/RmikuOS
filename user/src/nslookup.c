#include "user.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: nslookup <hostname>\n");
        return 1;
    }
    unsigned int ip = net_resolve(argv[1]);
    if (ip == 0) {
        printf("nslookup: can't resolve '%s'\n", argv[1]);
        return 1;
    }
    printf("%s -> %u.%u.%u.%u\n", argv[1],
           (ip >> 24) & 0xff, (ip >> 16) & 0xff,
           (ip >> 8) & 0xff, ip & 0xff);
    return 0;
}