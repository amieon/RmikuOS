#include "user.h"

int main(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { printf("socket failed\n"); return 1; }

    struct sockaddr_in host = addr_of(0x0A000202, 9999);
    printf("[user] connecting to 10.0.2.2:9999...\n");
    if (connect(fd, &host, sizeof host) < 0) {
        printf("[user] connect failed\n"); return 1;
    }
    printf("[user] connected!\n");

    char msg[] = "hello tcp from RmikuOS";
    if (send(fd, msg, sizeof(msg) - 1, 0) < 0) {
        printf("[user] send failed\n"); return 1;
    }
    printf("[user] sent, waiting echo...\n");

    char buf[512];
    for (;;) {
        int n = recv(fd, buf, sizeof(buf) - 1, 0);
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