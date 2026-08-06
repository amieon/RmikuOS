#include "test.h"

/* 文件系统:FAT 挂载点(/fat)多簇写读 + fsync 后重开验证真实落盘 */
int main() {
    TEST_START("fs_fat");

    /* FAT 可能未挂载:先探测 */
    struct stat st;
    if (stat("/fat", &st) < 0) {
        FAIL("FAT 未挂载(/fat 不存在),跳过");
        TEST_END();
    }

    const char *path = "/fat/fat_test.bin";
    unlink(path);

    int fd = open(path, O_CREAT | O_RDWR);
    CHECK(fd >= 0, "FAT 上创建文件成功");

    /* 写 64KB 数据(跨多簇,FAT16 簇大小 ~4KB => 16 簇) */
    char out[4096];
    for (int i = 0; i < 4096; i++) out[i] = (char)(i & 0xff);

    int total = 0, ok = 1;
    for (int round = 0; round < 16; round++) {
        isize w = write(fd, out, sizeof(out));
        if (w != (isize)sizeof(out)) { ok = 0; break; }
        total += (int)w;
    }
    CHECK(ok, "FAT 写入 64KB 全部成功");
    CHECK_EQ(total, 65536, "FAT 写入字节数正确");
    CHECK(fsync(fd) == 0, "fsync 刷盘成功");
    close(fd);

    /* 重开验证内容(读回部分校验) */
    fd = open(path, O_RDONLY);
    CHECK(fd >= 0, "重开文件成功");
    struct stat st2;
    CHECK(stat(path, &st2) == 0, "stat 成功");
    CHECK_EQ((isize)st2.st_size, 65536, "重开后文件大小正确");

    char in[4096];
    isize r = read(fd, in, sizeof(in));
    CHECK_EQ(r, 4096, "读回首块 4096 字节");
    int same = 1;
    for (int i = 0; i < 4096; i++) {
        if (in[i] != out[i]) { same = 0; break; }
    }
    CHECK(same, "首块内容与写入一致");
    close(fd);

    unlink(path);
    TEST_END();
}
