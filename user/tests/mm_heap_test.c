#include "test.h"

/* 堆分配:连续 malloc 不同大小 / 内容校验 / free 后重用 */
#define NBLOCKS 16

int main() {
    TEST_START("mm_heap");

    char *blocks[NBLOCKS];
    int sizes[NBLOCKS];
    int ok = 1;

    for (int i = 0; i < NBLOCKS; i++) {
        int size = 32 * (i + 1);
        char *p = malloc(size);
        if (!p) { ok = 0; break; }
        for (int j = 0; j < size; j++) p[j] = (char)j;
        blocks[i] = p;
        sizes[i] = size;
    }
    CHECK(ok, "连续 malloc 16 块(32~512B)成功");

    ok = 1;
    for (int i = 0; i < NBLOCKS; i++) {
        for (int j = 0; j < sizes[i]; j++) {
            if (blocks[i][j] != (char)j) { ok = 0; break; }
        }
        if (!ok) break;
    }
    CHECK(ok, "各块内容校验一致(无越界覆盖)");

    /* 释放后重新分配 */
    for (int i = 0; i < NBLOCKS; i++) free(blocks[i]);
    char *q = malloc(128);
    CHECK(q != 0, "free 后重新 malloc 成功");
    q[0] = 'A';
    CHECK(q[0] == 'A', "重用块可写");
    free(q);

    /* 大块(>1KB,走 first-fit 链) */
    char *big = malloc(4096);
    CHECK(big != 0, "malloc 4096B 大块成功");
    big[0] = 'B'; big[4095] = 'E';
    CHECK(big[0] == 'B' && big[4095] == 'E', "大块边界可读写");
    free(big);

    TEST_END();
}
