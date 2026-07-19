// 用法: tftp <remote> [local]
#include "user.h"
#define TFTP_SERVER 0x0A000202 /* 10.0.2.4,slirp 内置 TFTP/DHCP/DNS */
#define TFTP_PORT   69
#define BLKSIZE     512
#define MAX_FILE    (1024 * 1024) /* v1 先限制 1MB,够装文本和小文件 */


static int write_file(const char *path, const void *buf, unsigned int len){
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return -1;
    unsigned int done = 0;
    const char *p = buf;
    while (done < len) {
        int w = write(fd, p + done, len - done);
        if (w <= 0) { close(fd); return -1; }
        done += w;
    }
    close(fd);
    return done;
}

static unsigned short rd16(const unsigned char *p)
{
    return (unsigned short)((p[0] << 8) | p[1]);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: tftp <remote> [local]\n");
        return 1;
    }
    const char *remote = argv[1];
    const char *local  = argc > 2 ? argv[2] : argv[1];

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        printf("tftp: socket failed\n");
        return 1;
    }

    /* RRQ: [0,1, filename, 0, "octet", 0] */
    unsigned char req[544];
    int n = 0;
    req[n++] = 0; req[n++] = 1;
    for (const char *p = remote; *p; ) req[n++] = (unsigned char)*p++;
    req[n++] = 0;
    for (const char *p = "octet"; *p; ) req[n++] = (unsigned char)*p++;
    req[n++] = 0;
    struct sockaddr_in local_ = addr_of(0, 39069);   /* 0.0.0.0:39069,随便挑的高端口 */
    if (bind(fd, &local_, sizeof(local_)) < 0) {
        printf("tftp: bind failed\n");
        return 1;
    }
    struct sockaddr_in srv = addr_of(TFTP_SERVER, TFTP_PORT);
    sendto(fd, req, n, 0, &srv, sizeof(srv));

    static unsigned char file_buf[MAX_FILE]; /* 放 bss,别放栈 */
    unsigned int file_len = 0;
    unsigned short expect = 1;

    for (;;) {
        unsigned char pkt[4 + BLKSIZE];
        struct sockaddr_in from;
        /* 阻塞等包。v1 没有超时重传:slirp 到宿主机的 UDP 实际不丢包。
           若以后要有损环境,给 recvfrom 加超时后在此重发 RRQ/ACK。 */
        int r = recvfrom(fd, pkt, sizeof(pkt), 0, &from, 0);
        if (r < 4)
            continue;

        unsigned short op = rd16(pkt);
        if (op == 5) { /* ERROR: [0,5, errcode, msg, 0] */
            pkt[r - 1] = 0;
            printf("tftp: server error %d: %s\n", rd16(pkt + 2), (char *)(pkt + 4));
            return 1;
        }
        if (op != 3) /* 只要 DATA */
            continue;

        unsigned short blk = rd16(pkt + 2);
        if (blk != expect) {
            /* 重复 DATA(之前的 ACK 丢了):重发上一个 ACK */
            unsigned short prev = (unsigned short)(expect - 1);
            unsigned char ack[4] = { 0, 4,
                (unsigned char)(prev >> 8), (unsigned char)(prev & 0xFF) };
            sendto(fd, ack, 4, 0, &from, sizeof(from));
            continue;
        }

        int dlen = r - 4;
        if (file_len + (unsigned int)dlen <= MAX_FILE) {
            for (int i = 0; i < dlen; i++)
                file_buf[file_len + i] = pkt[4 + i];
            file_len += (unsigned int)dlen;
        }

        /* ACK 回 recvfrom 给出的地址——TFTP 服务器会从新端口(TID)回包,
           后续 ACK 必须发到 TID 而不是 69,用 from 就天然正确。 */
        unsigned char ack[4] = { 0, 4, pkt[2], pkt[3] };
        sendto(fd, ack, 4, 0, &from, sizeof(from));
        expect++;

        if (dlen < BLKSIZE) /* 短包 = 最后一块 */
            break;
    }

    if (write_file(local, file_buf, file_len) < 0) {
        printf("tftp: save %s failed\n", local);
        return 1;
    }
    printf("tftp: %s -> %s, %u bytes\n", remote, local, file_len);
    return 0;
}
