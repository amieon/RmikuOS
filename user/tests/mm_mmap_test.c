#include "test.h"

/* 虚拟内存基础:mmap 多页映射 / 映射内跨页读写 / munmap */
int main() {
    TEST_START("mm_mmap");

    /* 映射 8KB = 2 页,测多页映射的连续性 */
    char *p = mmap(8192, PROT_READ | PROT_WRITE);
    CHECK((isize)p >= 0, "mmap 分配 8KB(2 页)成功");

    p[0] = 'R'; p[1] = 'm'; p[2] = 'i'; p[3] = 'k'; p[4] = 'u'; p[5] = 0;
    CHECK_STREQ(p, "Rmiku", "mmap 内存可写可读");

    /* 跨页访问:4096 恰好是第 2 页起始,仍在映射范围内 */
    p[4096] = 'X';
    CHECK(p[4096] == 'X', "多页映射:第 2 页可读写");

    /* 映射末尾字节 */
    p[8191] = 'Z';
    CHECK(p[8191] == 'Z', "映射末尾字节可读写");

    CHECK(munmap(p, 8192) == 0, "munmap 释放成功");

    TEST_END();
}
