/* ============================================================================
 * ntpdate.c —— RmikuOS 的 NTP 客户端（RFC 5905 教学子集）
 *
 * 流程：
 *   1. 本地假时钟 = get_time_us()（单调微秒, 自启动起, 0 基准）
 *   2. 发 NTP 请求（VN=4 Mode=3, Origin=T1）→ 收响应（T2/T3）
 *   3. N 次采样, 每样本算 (offset, delay), 取 delay 最小（最对称=最准）
 *   4. 校准：当前 epoch 微秒 = (offset − 1900基准差) + 本地单调微秒
 *   5. set_wall_clock() 交给内核, 之后 time()/stat 时间戳单调累加
 *
 * 两种用法：
 *   ntpdate [-b] [端口]            纯数字 = 端口（旧用法）
 *   ntpdate [-b] <server> [端口]   server 支持 IP 或域名
 *   例：ntpdate 12300 ; ntpdate time.example.com ; ntpdate 10.0.2.2 12300
 *
 * -b（boot）：开机自动校时用。单样本、单次接收、全静默、失败也返回 0。
 *   两条硬约束决定了它必须长这样：
 *     ① 内核 recvfrom 是自旋忙等（syscall/net.rs 里 5000 万次后返回 0），
 *        服务器不通时**一次**接收就要好几秒，多次重试会把开机拖成分钟级。
 *        所以启动模式只发 1 个包、只收 1 次。
 *     ② 开机路径不能因为没网就失败。返回非 0 会让 login 判断出"出错了"，
 *        而这本来是完全正常的情况（离线机器）。真正的兜底是
 *        /var/lib/rtcstamp（drivers/rtc/stamp.rs），不是这里。
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

/* 纯数字串判断(用于区分"端口"与"服务器地址") */
static int is_all_digits(const char *s) {
    if (*s == '\0') return 0;
    while (*s) {
        if (*s < '0' || *s > '9') return 0;
        s++;
    }
    return 1;
}

/* 对一台服务器采样并校准墙钟。成功返回 0，失败 -1。
 *
 * quiet=1 时一个字都不打印：启动模式下 stdout 就是用户的登录界面，
 * 任何输出都会插在 "RmikuOS login:" 中间。 */
static int sync_from(unsigned long server_ip, int port,
                     int samples, int retry, int quiet) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        if (!quiet) fputs("ntpdate: socket failed\n", stdout);
        return -1;
    }
    struct sockaddr_in local = addr_of(0, 0);
    if (bind(fd, (struct sockaddr *)&local, sizeof local) < 0) {
        if (!quiet) fputs("ntpdate: bind failed\n", stdout);
        close(fd);
        return -1;
    }
    struct sockaddr_in srv = addr_of(server_ip, port);

    unsigned long best_delay = ~0UL, best_offset = 0;
    int got = 0;

    for (int i = 0; i < samples; i++) {
        unsigned char pkt[NTP_PKT_LEN] = {0};
        pkt[0] = 0x1B; /* LI=0 VN=4 Mode=3(client) */
        unsigned long t1 = local_ntp64();
        put_u64(pkt + 24, t1); /* Origin timestamp */
        if (sendto(fd, pkt, NTP_PKT_LEN, 0, (struct sockaddr *)&srv, sizeof srv) < 0) continue;

        unsigned long t2 = 0, t3 = 0;
        int ok = 0;
        for (int r = 0; r < retry; r++) {
            unsigned char buf[512];
            struct sockaddr_in from = {0};
            int n = recvfrom(fd, buf, sizeof buf, 0, (struct sockaddr *)&from, 0);
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
    close(fd);

    if (!got) {
        if (!quiet) fputs("ntpdate: no reply\n", stdout);
        return -1;
    }

    /* 校准: 当前 epoch 微秒 = (offset − 1900基准差) + 本地单调微秒 */
    long off_sec = (long)(best_offset >> 32) - (long)NTP_EPOCH_OFFSET;
    unsigned long off_frac = best_offset & 0xFFFFFFFFUL;
    unsigned long epoch_us = (unsigned long)off_sec * 1000000
                           + ((off_frac * 1000000) >> 32)
                           + (unsigned long)get_time_us();
    set_wall_clock(epoch_us);

    if (!quiet) {
        printf("[ntpdate] synced: epoch=%lu s, delay=%lu ms\n",
               epoch_us / 1000000,
               (best_delay * 1000) >> 32);
    }

    /* 顺手存下来，下次开机内核直接读（fake-hwclock，见 drivers/rtc/stamp.rs）。
     * 存不下只是"下次开机没有历史值"，不该让本次校时算失败，所以只警告。 */
    if (hwclock_stamp_write(epoch_us / 1000000) != 0) {
        if (!quiet) fputs("[ntpdate] warning: cannot save hwclock stamp\n", stdout);
    }
    return 0;
}

/* 开机模式：按顺序试几个服务器，第一个成功的就够。
 *
 * 顺序有讲究：10.0.2.2 是 QEMU slirp 网关，宿主上跑了 tools/ntp_server.py
 * 时会**毫秒级**回包（本地回环，没有 DNS，没有外网）；
 * 公网那两个在宿主能出网时可用，但先要一次 DNS 查询。
 *
 * 注意 slirp **自己不实现 NTP**，10.0.2.2:123 只在宿主真的起了服务时才通；
 * 不通的时候是"发出去没人理"，只能等满一次接收超时——所以单次尝试必须是
 * 单样本单接收，不能像交互模式那样 5 样本 × 20 次重试。 */
static const char *const boot_servers[] = {
    "10.0.2.2",        /* QEMU slirp 网关（宿主 tools/ntp_server.py） */
    "ntp.aliyun.com",  /* 公网；走 slirp NAT */
};

int main(int argc, char *argv[]) {
    int port = NTP_PORT;
    const char *server_str = 0;
    unsigned long server_ip = 0x0A000202UL; /* 默认 10.0.2.2 = QEMU slirp 网关 */
    int argi = 1;

    int boot = 0;
    if (argc > argi && strcmp(argv[argi], "-b") == 0) {
        boot = 1;
        argi++;
    }

    /* ---- 开机模式 ---- */
    if (boot) {
        int n = (int)(sizeof boot_servers / sizeof boot_servers[0]);
        for (int i = 0; i < n; i++) {
            unsigned long ip = parse_ip(boot_servers[i]);
            if (ip == 0) ip = net_resolve(boot_servers[i]);
            if (ip == 0) continue;            /* DNS 挂了就换下一个，不报错 */
            if (sync_from(ip, NTP_PORT, 1, 1, 1) == 0) return 0;
        }
        return 0;   /* 全失败是正常情况：stamp 已经兜过底了 */
    }

    /* ---- 交互模式 ---- */
    /* 用法: ntpdate [端口] 或 ntpdate <server> [端口](server 支持 IP 或域名) */
    if (argc > argi) {
        if (is_all_digits(argv[argi])) {
            port = atoi(argv[argi]);       /* 旧用法:纯数字 = 端口 */
        } else {
            server_str = argv[argi];
            if (argc > argi + 1) port = atoi(argv[argi + 1]);
        }
    }
    if (server_str) {
        unsigned int ip = parse_ip(server_str);
        if (ip == 0) ip = net_resolve(server_str);
        if (ip == 0) {
            fputs("ntpdate: cannot resolve server\n", stdout);
            return 1;
        }
        server_ip = ip;
    }

    return sync_from(server_ip, port, SAMPLES, RECV_RETRY, 0) == 0 ? 0 : 1;
}
