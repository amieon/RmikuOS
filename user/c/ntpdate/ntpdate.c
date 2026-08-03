/* ============================================================================
 * ntpdate.c —— RmikuOS 的 NTP 客户端（RFC 5905 教学子集）
 *
 * 流程：
 *   1. 本地假时钟 = get_time_us()（单调微秒, 自启动起, 0 基准）
 *   2. 发 NTP 请求（VN=4 Mode=3, Origin=T1）→ 收响应（T2/T3）
 *   3. 5 次采样, 每样本算 (offset, delay), 取 delay 最小（最对称=最准）
 *   4. 校准：当前 epoch 微秒 = (offset − 1900基准差) + 本地单调微秒
 *   5. set_wall_clock() 交给内核, 之后 time()/stat 时间戳单调累加
 *
 * 服务器：默认 QEMU slirp 网关 10.0.2.2:123（host 上跑 tools/ntp_server.py）。
 * 用法：ntpdate [端口]   例：ntpdate 12300
 * ==========================================================================*/
#include "user.h"

#define NTP_EPOCH_OFFSET 2208988800UL /* 1900-01-01 到 1970-01-01 的秒数 */
#define NTP_PORT 123
#define NTP_PKT_LEN 48
#define SAMPLES 5
#define RECV_RETRY 20

/* NTP 64 位时间戳（大端）: 高 32=秒, 低 32=分数秒(2^-32) */
static unsigned long get_u64(const unsigned char *p) {
    return ((unsigned long)p[0] << 56) | ((unsigned long)p[1] << 48) |
           ((unsigned long)p[2] << 40) | ((unsigned long)p[3] << 32) |
           ((unsigned long)p[4] << 24) | ((unsigned long)p[5] << 16) |
           ((unsigned long)p[6] << 8) | (unsigned long)p[7];
}
static void put_u64(unsigned char *p, unsigned long v) {
    for (int i = 0; i < 8; i++) p[i] = (unsigned char)(v >> (56 - 8 * i));
}

/* 本地假时钟 → NTP 64 位格式（秒=开机以来, 0 基准） */
static unsigned long local_ntp64(void) {
    unsigned long us = (unsigned long)get_time_us();
    unsigned long secs = us / 1000000;
    unsigned long frac = ((us % 1000000) << 32) / 1000000;
    return (secs << 32) | frac;
}

int main(int argc, char *argv[]) {
    int port = NTP_PORT;
    unsigned long server_ip = 0x0A000202UL; /* 10.0.2.2 = QEMU slirp 网关 */
    if (argc > 1) port = atoi(argv[1]);

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { fputs("ntpdate: socket failed\n", stdout); return 1; }
    struct sockaddr_in local = addr_of(0, 0);
    if (bind(fd, &local, sizeof local) < 0) {
        fputs("ntpdate: bind failed\n", stdout); return 1;
    }
    struct sockaddr_in srv = addr_of(server_ip, port);

    unsigned long best_delay = ~0UL, best_offset = 0;
    int got = 0;

    for (int i = 0; i < SAMPLES; i++) {
        unsigned char pkt[NTP_PKT_LEN] = {0};
        pkt[0] = 0x1B; /* LI=0 VN=4 Mode=3(client) */
        unsigned long t1 = local_ntp64();
        put_u64(pkt + 24, t1); /* Origin timestamp */
        if (sendto(fd, pkt, NTP_PKT_LEN, 0, &srv, sizeof srv) < 0) continue;

        unsigned long t2 = 0, t3 = 0;
        int ok = 0;
        for (int r = 0; r < RECV_RETRY; r++) {
            unsigned char buf[512];
            struct sockaddr_in from = {0};
            int n = recvfrom(fd, buf, sizeof buf, 0, &from, 0);
            if (n >= NTP_PKT_LEN) {
                t2 = get_u64(buf + 32); /* Receive timestamp */
                t3 = get_u64(buf + 40); /* Transmit timestamp */
                ok = 1;
                break;
            }
        }
        if (!ok) continue;
        unsigned long t4 = local_ntp64();

        /* 64 位定点(2^-32 秒)运算。注意防溢出: t2-t1 ≈ 39.9亿秒<<32 ≈ 1.7e19,
         * 两个相加会超 2^64 回绕 —— 必须先各自 >>1 再相加(丢 0.5 单位≈0.1ns)。 */
        unsigned long delay = (t4 - t1) - (t3 - t2);
        unsigned long offset = ((t2 - t1) >> 1) + ((t3 - t4) >> 1);
        if (delay < best_delay) {
            best_delay = delay;
            best_offset = offset;
            got = 1;
        }
    }
    if (!got) { fputs("ntpdate: no reply\n", stdout); return 1; }

    /* 校准: 当前 epoch 微秒 = (offset − 1900基准差) + 本地单调微秒 */
    long off_sec = (long)(best_offset >> 32) - (long)NTP_EPOCH_OFFSET;
    unsigned long off_frac = best_offset & 0xFFFFFFFFUL;
    unsigned long epoch_us = (unsigned long)off_sec * 1000000
                           + ((off_frac * 1000000) >> 32)
                           + (unsigned long)get_time_us();
    set_wall_clock(epoch_us);

    printf("[ntpdate] synced: epoch=%lu s, delay=%lu ms\n",
           epoch_us / 1000000,
           (best_delay * 1000) >> 32);
    return 0;
}
