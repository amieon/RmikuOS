// 用法: nslookup <hostname>
// 经内核 DNS 客户端解析域名 -> 只输出 IPv4(dig +short 风格,方便命令替换/管道组合)
// 失败: stdout 无输出,错误信息走 stderr,退出码 1
#include "user.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: nslookup <hostname>\n");
        return 1;
    }
    unsigned int ip = net_resolve(argv[1]);
    if (ip == 0) {
        fprintf(stderr, "nslookup: can't resolve '%s'\n", argv[1]);
        return 1;
    }
    printf("%u.%u.%u.%u\n",
           (ip >> 24) & 0xff, (ip >> 16) & 0xff,
           (ip >> 8) & 0xff, ip & 0xff);
    return 0;
}