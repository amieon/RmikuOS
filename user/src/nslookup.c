// 用法: nslookup <hostname> [hostname ...]
// 单个域名: 只输出 IPv4(dig +short 风格,方便命令替换/管道组合)
// 多个域名: 一次批量解析(并发,≈1 个 RTT),每行 "域名 -> IP";失败走 stderr
#include "user.h"

static void print_ip(unsigned int ip) {
    printf("%u.%u.%u.%u\n",
           (ip >> 24) & 0xff, (ip >> 16) & 0xff,
           (ip >> 8) & 0xff, ip & 0xff);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: nslookup <hostname> [hostname ...]\n");
        return 1;
    }

    /* 单个域名:走 net_resolve(带 TTL 缓存),输出纯 IP 保持可组合 */
    if (argc == 2) {
        unsigned int ip = net_resolve(argv[1]);
        if (ip == 0) {
            fprintf(stderr, "nslookup: can't resolve '%s'\n", argv[1]);
            return 1;
        }
        print_ip(ip);
        return 0;
    }

    /* 多个域名:一次 net_resolve_many 并发批量解析 */
    int n = argc - 1;
    if (n > 64) n = 64;                 // 与内核 count 上限对齐
    const char *names[64];
    unsigned long lens[64];
    unsigned int out[64];
    for (int i = 0; i < n; i++) {
        names[i] = argv[i + 1];
        unsigned int len = 0;
        while (names[i][len]) len++;
        lens[i] = len;
    }

    int ret = net_resolve_many(names, lens, out, n);
    if (ret < 0) {
        fprintf(stderr, "nslookup: batch resolve failed\n");
        return 1;
    }

    int fail = 0;
    for (int i = 0; i < n; i++) {
        if (out[i] == 0) {
            fprintf(stderr, "nslookup: can't resolve '%s'\n", names[i]);
            fail = 1;
        } else {
            printf("%s -> %u.%u.%u.%u\n", names[i],
                   (out[i] >> 24) & 0xff, (out[i] >> 16) & 0xff,
                   (out[i] >> 8) & 0xff, out[i] & 0xff);
        }
    }
    return fail ? 1 : 0;
}
