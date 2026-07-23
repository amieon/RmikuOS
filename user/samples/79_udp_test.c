#include "user.h"

int main(void) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { printf("socket failed\n"); return 1; }

    struct sockaddr_in local = addr_of(0, 12345);
    if (bind(fd, &local, sizeof local) < 0) { printf("bind failed\n"); return 1; }

    struct sockaddr_in host = addr_of(0x0A000202, 9999);  // 10.0.2.2 slirp 网关
    char msg[] = "hello from RmikuOS user";
    if (sendto(fd, msg, sizeof(msg) - 1, 0, &host, sizeof host) < 0) {
        printf("sendto failed\n"); return 1;
    }
    printf("[user] sent, waiting reply...\n");

    char buf[512];
    for (int retry = 0; retry < 10; retry++) {
        struct sockaddr_in from = {0};
        int n = recvfrom(fd, buf, sizeof(buf) - 1, 0, &from, 0);
        if (n > 0) {
            buf[n] = 0;
            unsigned ip = ntohl(from.sin_addr);
            printf("[user] got %d bytes from %u.%u.%u.%u:%u\n", n,
                   (ip >> 24) & 0xff, (ip >> 16) & 0xff,
                   (ip >> 8) & 0xff, ip & 0xff,
                   ntohs(from.sin_port));
            printf("[user] payload: %s\n", buf);
            net_close(fd);
            return 0;
        }
        printf("[user] recv timeout, retry %d\n", retry + 1);
    }
    printf("[user] no reply\n");
    net_close(fd);
    return 1;
}