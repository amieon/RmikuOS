/* pread / pwrite 测试
 *
 * 核心语义：带偏移的读写**不改变文件偏移**（POSIX pread/pwrite）。
 * 这条语义的意义在并发：多个线程/进程共享同一 fd 时，lseek+read 的组合
 * 会互相踩偏移，而 pread 各自带 offset，无需加锁。
 *
 * 覆盖：
 *   1. pread 读中间片段，且不动偏移
 *   2. pwrite 覆盖中间片段，且不动偏移
 *   3. 读/写越过 EOF 的行为（pread 返回 0 / 部分读）
 *   4. pipe 这类无偏移对象上 pread 返回 -1（对应 Linux -ESPIPE）
 *   5. O_APPEND 打开时 pwrite 忽略 offset 写到末尾
 */
#include "user.h"

static int fails = 0;

static void check(int cond, const char *msg) {
    if (cond) {
        puts("  OK   ");
        puts(msg);
        put_char('\n');
    } else {
        puts("  FAIL ");
        puts(msg);
        put_char('\n');
        fails++;
    }
}

int main() {
    const char *path = "/tmp/pt.bin";
    char buf[32];
    isize n;

    puts("=== pread/pwrite 测试 ===\n");

    /* ---- 准备：写入 "0123456789"，此时偏移 = 10 ---- */
    int fd = open(path, O_CREAT | O_RDWR | O_TRUNC);
    check(fd >= 0, "open(O_CREAT|O_RDWR|O_TRUNC)");
    n = write(fd, "0123456789", 10);
    check(n == 10, "write 10 字节");

    /* ---- 1. pread 不改变偏移 ---- */
    n = pread(fd, buf, 4, 3);
    buf[n] = 0;
    check(n == 4 && buf[0] == '3' && buf[3] == '6', "pread(off=3,len=4) 读到 \"3456\"");

    /* 偏移若被 pread 动过，这里会读到 "3456..."；没动则是 EOF(0) */
    n = read(fd, buf, 8);
    check(n == 0, "pread 之后 read 返回 0 -> 偏移没被 pread 改动");

    /* ---- 2. pwrite 不改变偏移 ---- */
    n = pwrite(fd, "XY", 2, 5);
    check(n == 2, "pwrite(off=5,\"XY\") 写 2 字节");
    n = lseek(fd, 0, SEEK_CUR);
    check(n == 10, "pwrite 之后偏移仍是 10");

    n = pread(fd, buf, 10, 0);
    buf[n] = 0;
    check(n == 10 && buf[5] == 'X' && buf[6] == 'Y', "pread 全量 -> \"01234XY789\"");

    /* ---- 3. 越过 EOF ---- */
    n = pread(fd, buf, 4, 100);
    check(n == 0, "pread 越过 EOF 返回 0");
    n = pread(fd, buf, 8, 8);
    buf[n] = 0;
    check(n == 2 && buf[0] == '8' && buf[1] == '9', "pread 部分越过 EOF 只返回剩余 2 字节");

    /* ---- 4. pipe 上没有偏移概念 ---- */
    int pfd[2];
    pipe(pfd);
    n = pread(pfd[0], buf, 4, 0);
    check(n == -1, "pipe 上 pread 返回 -1(对应 Linux -ESPIPE)");
    n = pwrite(pfd[1], "hi", 2, 0);
    check(n == -1, "pipe 上 pwrite 返回 -1(对应 Linux -ESPIPE)");

    /* ---- 5. O_APPEND + pwrite 写到末尾 ---- */
    int afd = open(path, O_WRONLY | O_APPEND);
    check(afd >= 0, "open(O_WRONLY|O_APPEND)");
    n = pwrite(afd, "ZZ", 2, 0);
    check(n == 2, "O_APPEND 下 pwrite(off=0,\"ZZ\")");
    close(afd);
    n = pread(fd, buf, 12, 0);
    buf[n] = 0;
    check(n == 12 && buf[10] == 'Z' && buf[11] == 'Z',
          "O_APPEND 的 pwrite 忽略 offset 写到末尾 -> \"01234XY789ZZ\"");
    n = lseek(fd, 0, SEEK_CUR);
    check(n == 10, "O_APPEND 的 pwrite 也没动本 fd 的偏移");

    close(fd);

    if (fails == 0) {
        puts("\n全部通过\n");
        return 0;
    }
    printf("\n%d 项失败\n", fails);
    return 1;
}
