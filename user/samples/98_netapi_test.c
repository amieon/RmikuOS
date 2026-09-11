/* netapi_test.c —— getsockname(115) / getpeername(116) / setsockopt(117) 验证
 *
 * 第一段(地址查询): 需要宿主机 python3 halfclose_server.py  (9999 端口)
 * 第二段(REUSEADDR): 走 8080(hostfwd 已通),按提示在宿主机执行
 *                    curl 127.0.0.1:8080/  制造一个 TimeWait 尸体
 *
 * 用法: netapi_test
 *
 * 检查点:
 *   [1] 未连接 socket 的 getpeername 必须失败(-1)
 *   [2] connect 后 getpeername == 10.0.2.2:9999
 *   [3] getsockname 本端 == 10.0.2.15 + 非零临时端口
 *   [4] UDP bind(0) 自动分配端口,getsockname 能读回(>=20000)
 *   [5] TimeWait 尸体占用端口:无 REUSEADDR 的 bind 必须失败
 *   [6] 设置 SO_REUSEADDR 后 bind 同端口成功
 */
#include "user.h"

static void print_addr(const char *tag, struct sockaddr_in *a) {
    unsigned int ip = ntohl(a->sin_addr);
    printf("%s %u.%u.%u.%u:%u\n", tag,
           (ip >> 24) & 0xff, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff,
           ntohs(a->sin_port));
}

static void wait_ticks(unsigned long t) {
    unsigned long t0 = get_ticks();
    while (get_ticks() - t0 < t) ;
}

int main(void) {
    int pass = 0, fail = 0;
    struct sockaddr_in a;

    /* ========== 第一段:getsockname / getpeername ========== */
    printf("=== part 1: getsockname / getpeername ===\n");

    /* [1] 未连接就查对端 -> 必须 -1 */
    int fd = socket_tcp();
    if (net_getpeername(fd, &a) < 0) { printf("[PASS] [1] getpeername before connect -> -1\n"); pass++; }
    else                             { printf("[FAIL] [1] getpeername before connect should fail\n"); fail++; }

    /* [2] connect 后查对端 */
    struct sockaddr_in srv = addr_of(0x0A000202, 9999);   /* 10.0.2.2:9999 */
    if (connect(fd, &srv, sizeof srv) < 0) {
        printf("[FAIL] connect 10.0.2.2:9999 (halfclose_server.py 起了吗?)\n");
        net_close(fd);
        return 1;
    }
    if (net_getpeername(fd, &a) == 0) {
        print_addr("       [2] peer =", &a);
        if (ntohl(a.sin_addr) == 0x0A000202 && ntohs(a.sin_port) == 9999) {
            printf("[PASS] [2] peername == 10.0.2.2:9999\n"); pass++;
        } else { printf("[FAIL] [2] peername wrong value\n"); fail++; }
    } else { printf("[FAIL] [2] getpeername after connect -> -1\n"); fail++; }

    /* [3] 查本端:10.0.2.15 + 临时端口 */
    if (net_getsockname(fd, &a) == 0) {
        print_addr("       [3] local =", &a);
        if (ntohl(a.sin_addr) == 0x0A00020F && ntohs(a.sin_port) != 0) {
            printf("[PASS] [3] sockname == 10.0.2.15 + ephemeral port\n"); pass++;
        } else { printf("[FAIL] [3] sockname wrong value\n"); fail++; }
    } else { printf("[FAIL] [3] getsockname -> -1\n"); fail++; }
    net_close(fd);

    /* [4] UDP bind(0) 自动分配端口 */
    int ufd = socket_udp();
    struct sockaddr_in any = addr_of(0, 0);
    if (bind(ufd, &any, sizeof any) < 0) {
        printf("[FAIL] [4] udp bind(0) failed\n"); fail++;
    } else if (net_getsockname(ufd, &a) == 0 && ntohs(a.sin_port) >= 20000) {
        print_addr("       [4] udp auto port =", &a);
        printf("[PASS] [4] getsockname reads back auto-assigned port\n"); pass++;
    } else { printf("[FAIL] [4] getsockname after udp bind(0)\n"); fail++; }
    net_close(ufd);

    /* ========== 第二段:SO_REUSEADDR vs TimeWait ========== */
    printf("\n=== part 2: SO_REUSEADDR vs TIME_WAIT ===\n");
    printf(">>> 现在去宿主机执行: curl 127.0.0.1:8080/  (10 秒内)\n");

    int lfd = socket_tcp();
    struct sockaddr_in laddr = addr_of(0, 8080);
    if (bind(lfd, &laddr, sizeof laddr) < 0 || listen(lfd, 4) < 0) {
        printf("[FAIL] part2 listen setup (8080 被占了?等 10s 再跑)\n");
        net_close(lfd);
        return 1;
    }
    int cfd = -1;
    unsigned long t0 = get_ticks();
    while (get_ticks() - t0 < 1000) {          /* 等 curl,~10s */
        cfd = accept(lfd, 0, 0);
        if (cfd >= 0) break;
    }
    if (cfd < 0) {
        printf("[FAIL] part2 no connection (没 curl?)\n");
        net_close(lfd);
        return 1;
    }
    printf("       accepted, now ACTIVELY close -> 我方进 TimeWait\n");
    net_close(cfd);                             /* 主动关:尸体占用 8080 */
    net_close(lfd);
    wait_ticks(300);                            /* 等四次挥手走完进 TimeWait */

    /* [5] 不设 REUSEADDR:bind 必须失败(尸体占端口) */
    int f2 = socket_tcp();
    if (bind(f2, &laddr, sizeof laddr) < 0) {
        printf("[PASS] [5] bind without REUSEADDR blocked by TIME_WAIT corpse\n"); pass++;
    } else {
        printf("[FAIL] [5] bind succeeded without REUSEADDR (尸体没拦住?)\n"); fail++;
    }
    net_close(f2);

    /* [6] 设 REUSEADDR:bind 同端口必须成功 */
    int f3 = socket_tcp();
    net_setsockopt(f3, SO_REUSEADDR, 1);
    if (bind(f3, &laddr, sizeof laddr) == 0) {
        printf("[PASS] [6] bind with SO_REUSEADDR succeeds over TIME_WAIT\n"); pass++;
    } else {
        printf("[FAIL] [6] bind with SO_REUSEADDR still failed\n"); fail++;
    }
    net_close(f3);

    printf("\n==== %d PASS, %d FAIL ====\n", pass, fail);
    return fail ? 1 : 0;
}