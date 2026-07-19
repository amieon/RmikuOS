#include "user.h"

int main(int argc, char **argv) {
    printf("MY_IP = %u.%u.%u.%u\n",
           (net_get_ip() >> 24) & 0xff, (net_get_ip() >> 16) & 0xff,
           (net_get_ip() >> 8) & 0xff, net_get_ip() & 0xff);
    return 0;
}