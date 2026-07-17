// tcp_test.c
#include "user.h"

int main(void) {
    int fd = net_socket(1);                 // 1 = TCP
    if (fd < 0) { printf("socket failed\n"); return 1; }

    printf("[user] connecting to 10.0.2.2:9999...\n");
    if (net_connect(fd, 0x0A000202, 9999) < 0) {
        printf("[user] connect failed\n"); return 1;
    }
    printf("[user] connected!\n");

    char msg[] = "hello tcp from RmikuOS";
    if (net_send(fd, msg, sizeof(msg) - 1) < 0) {
        printf("[user] send failed\n"); return 1;
    }
    printf("[user] sent, waiting echo...\n");

    char buf[512];
    for (;;) {
        int n = net_recv(fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = 0;
            printf("[user] got %d bytes: %s\n", n, buf);
            break;
        } else if (n == 0) {
            printf("[user] peer closed (EOF)\n");
            break;
        } else {
            printf("[user] connection reset\n");
            break;
        }
    }
    net_close(fd);
    printf("[user] closed\n");
    return 0;
}