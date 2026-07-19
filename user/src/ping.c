#include "user.h"

#define PING_ID 0x3950

static unsigned short cksum(const void *data, int len) {
    const unsigned short *p = data;
    unsigned int sum = 0;
    while (len > 1) { sum += *p++; len -= 2; }
    if (len) sum += *(const unsigned char *)p;
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (unsigned short)~sum;
}

struct icmp_hdr { unsigned char type, code; unsigned short cksum, id, seq; };

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: ping a.b.c.d\n");
        return 1;
    }
    unsigned int target = parse_ip(argv[1]);   // "192.168.100.2" -> 0xC0A86402(主机序)
    if (target == 0) {
        printf("ping: bad address %s\n", argv[1]);
        return 1;
    }

    int fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (fd < 0) { printf("ping: socket failed\n"); return 1; }

    struct sockaddr_in gw = addr_of(target, 0); 
    

    //struct sockaddr_in gw = addr_of(0x0A000202, 0);   // 10.0.2.2 网关(ICMP 无端口)
    int sent = 0, rcvd = 0;

    for (int seq = 1; seq <= 4; seq++) {
        unsigned char pkt[64] = {0};
        struct icmp_hdr *h = (void *)pkt;
        h->type = 8; h->id = htons(PING_ID); h->seq = htons(seq);
        for (int i = 8; i < 64; i++) pkt[i] = i;
        h->cksum = 0;
        h->cksum = cksum(pkt, 64);

        unsigned long t0 = get_ticks();
        if (sendto(fd, pkt, 64, 0, &gw, sizeof gw) < 0) {
            printf("sendto failed\n"); return 1;
        }
        sent++;

        for (;;) {
            unsigned char buf[256];
            struct sockaddr_in from = {0};
            int n = recvfrom(fd, buf, sizeof buf, 0, &from, 0);
            if (n >= 8) {
                struct icmp_hdr *r = (void *)buf;
                if (r->type == 0 && r->id == htons(PING_ID) && r->seq == htons(seq)) {
                    unsigned ip = ntohl(from.sin_addr);
                    unsigned char *p = (unsigned char *)&from.sin_addr;
                    printf("64 bytes from %u.%u.%u.%u: icmp_seq=%d time=%d ticks\n",
                        p[0], p[1], p[2], p[3], seq, get_ticks() - t0);
                    // printf("64 bytes from %u.%u.%u.%u: icmp_seq=%d time=%lu ticks\n",
                    //        (ip>>24)&0xff, (ip>>16)&0xff, (ip>>8)&0xff, ip&0xff,
                    //        seq, get_ticks() - t0);
                    rcvd++;
                    break;
                }
            }
            if (get_ticks() - t0 > 1000) {
                printf("Request timeout for icmp_seq %d\n", seq);
                break;
            }
        }
    }
    printf("--- ping statistics ---\n");
    printf("%d packets transmitted, %d received, %d%% packet loss\n",
           sent, rcvd, (sent - rcvd) * 100 / sent);
    net_close(fd);
    return rcvd > 0 ? 0 : 1;
}