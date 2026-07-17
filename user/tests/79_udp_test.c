#include "user.h"

int main(void) {
    int fd = net_socket(2);
    if (fd < 0) { printf("socket failed\n"); return 1; }
    if (net_bind(fd, 12345) < 0) { printf("bind failed\n"); return 1; }

    unsigned ip = 0x0A000202; // 10.0.2.2 = slirp 网关 = host
    char msg[] = "hello from RmikuOS user";
    if (net_sendto(fd, msg, sizeof(msg) - 1, ip, 9999) < 0) {
        printf("sendto failed\n"); return 1;
    }
    printf("[user] sent, waiting reply...\n");

    char buf[512];
    unsigned char info[8];
    for (int retry = 0; retry < 10; retry++) {
        int n = net_recvfrom(fd, buf, sizeof(buf) - 1, info);
        if (n > 0) {
            buf[n] = 0;
            printf("[user] got %d bytes from %d.%d.%d.%d:%d\n", n,
                   info[0], info[1], info[2], info[3],
                   (info[4] << 8) | info[5]);
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