/* getrandom / virtio-rng 测试
 *
 * 内核侧熵源是 virtio-rng（kernel/src/drivers/rng/），getrandom(2) 是它唯一的
 * 出口（syscall 127）。这里**不测"随机数够不够好"** —— 跑几百 KB 做统计检验
 * 证明不了密码学性质，那是另一门学科。这里只测三件确定的事：
 *
 *   1. 语义与 Linux 对齐：len=0 / buf=NULL / 非法 flags 的返回值和 errno。
 *      errno 值必须和内核 syscall/errno.rs 一一对应，否则上层只会看到
 *      "报了个不相干的错"（这个项目踩过）。
 *   2. 不退化。真正踩过的坑不是"熵低"，是 fill_bytes 里一句 output.fill(0)：
 *      返回值全是成功，字节全是 0。所以下面的断言全是"抓常量输出"的形状 ——
 *      全零块、块重复、比特偏置、单字节恒定。
 *   3. 分块拼装。内核一次只从设备拿一页，超过 256 字节要循环切块写用户缓冲；
 *      循环写错（比如 offset 没推进）会把同一段覆写 32 次 —— 大块读 + 逐块
 *      两两比较能直接抓到。
 */
#include "test.h"
#include "unistd.h"

#define BLOCKS 512

/* 512 块 × 16 字节，放静态区：8 KiB 栈上放得下，但没必要冒险 */
static unsigned char blocks[BLOCKS][16];

int main() {
    unsigned char buf[256];

    TEST_START("rand");

    /* ---------- 0. 设备在不在 ----------
     * 没有 virtio-rng 时内核明确返回 -EAGAIN（绝不返回"假随机"）。
     * 这种情况下按项目的 skip 约定返回 77（run_all 会记 SKIP），但先断言
     * "必须是 EAGAIN" —— 它本身是一条契约：宁可失败，也不要给调用方一个
     * 看着成功的零。返回别的错码才说明真的坏了。 */
    errno = 0;
    ssize_t n = getrandom(buf, 16, 0);
    if (n < 0) {
        printf("[INFO] getrandom 失败: 返回 %d, errno=%d\n", (int)n, errno);
        if (errno != EAGAIN) {
            FAIL("无熵源时必须报 EAGAIN，不能是别的错（-EINVAL/-EPERM 之类）");
            TEST_END();
        }
        printf("[SKIP] 没有 virtio-rng 设备（返回 -EAGAIN），跳过随机性检查\n");
        return 77;
    }
    CHECK_EQ(n, 16, "getrandom(16) 返回 16 字节");

    /* ---------- 1. 边界语义 ---------- */
    errno = 0;
    CHECK_EQ(getrandom(buf, 0, 0), 0, "len=0 返回 0（不碰 buf）");

    errno = 0;
    CHECK_EQ(getrandom(0, 16, 0), -1, "buf=NULL 返回 -1");
    CHECK_EQ(errno, EFAULT, "...并且 errno=EFAULT");

    errno = 0;
    CHECK_EQ(getrandom(buf, 16, 0x8), -1, "未定义的 flags 位返回 -1");
    CHECK_EQ(errno, EINVAL, "...并且 errno=EINVAL");

    /* 0x1|0x2|0x4 = NONBLOCK|RANDOM|INSECURE，本内核全接受（语义为空） */
    CHECK_EQ(getrandom(buf, 16, 0x7), 16, "已定义的三个 flags 都接受");

    /* ---------- 2. 不许是常量 ---------- */
    int zero_blocks = 0;
    int short_read = 0;
    for (int i = 0; i < BLOCKS; i++) {
        if (getrandom(blocks[i], 16, 0) != 16) {
            short_read++;
            continue;
        }
        int all_zero = 1;
        for (int j = 0; j < 16; j++) {
            if (blocks[i][j] != 0) { all_zero = 0; break; }
        }
        if (all_zero) zero_blocks++;
    }
    CHECK_EQ(short_read, 0, "512 次取 16 字节全部成功");
    CHECK_EQ(zero_blocks, 0, "没有任何一块全 0（fill(0) 那种退化会全中）");

    /* 512 块撞一对的概率是 512*511/2 / 2^128，比连续中彩票还低 */
    int dup = 0;
    for (int i = 0; i < BLOCKS; i++) {
        for (int j = i + 1; j < BLOCKS; j++) {
            if (memcmp(blocks[i], blocks[j], 16) == 0) dup++;
        }
    }
    CHECK_EQ(dup, 0, "512 块两两不同");

    /* ---------- 3. 比特与字节分布 ---------- */
    unsigned char big[4096];
    CHECK_EQ(getrandom(big, sizeof big, 0), (long)sizeof big, "一次读 4096 字节");

    long ones = 0;
    int hist[256];
    for (int i = 0; i < 256; i++) hist[i] = 0;

    for (usize i = 0; i < sizeof big; i++) {
        unsigned char b = big[i];
        hist[b]++;
        for (int k = 0; k < 8; k++) ones += (b >> k) & 1;
    }
    /* 32768 个比特，1 的个数标准差约 90。45%~55% 是 ±1638，等于 18 个标准差，
     * 正常设备不可能越界；而"只用了 7 位"这类偏置一定会被抓到。 */
    long lo = 32768L * 45 / 100;
    long hi = 32768L * 55 / 100;
    printf("[INFO] 4096 字节里 1 的个数 = %ld（期望 14746~18022）\n", ones);
    CHECK(ones > lo && ones < hi, "1 的占比落在 45%~55%");

    int missing = 0;
    for (int i = 0; i < 256; i++) {
        if (hist[i] == 0) missing++;
    }
    CHECK_EQ(missing, 0, "4096 字节覆盖全部 256 种字节值");

    /* ---------- 4. 单字节多次调用也不许恒定 ---------- */
    unsigned char one[256];
    for (int i = 0; i < 256; i++) {
        if (getrandom(one + i, 1, 0) != 1) one[i] = 0;
    }
    int all_same = 1;
    for (int i = 1; i < 256; i++) {
        if (one[i] != one[0]) { all_same = 0; break; }
    }
    CHECK_EQ(all_same, 0, "256 次单字节调用不返回同一个值");

    /* ---------- 5. 大块分块拼装（内核按 256 字节切块） ---------- */
    static unsigned char huge[8192];
    CHECK_EQ(getrandom(huge, sizeof huge, 0), (long)sizeof huge,
             "一次读 8192 字节（跨 32 个内核分块）");

    int chunk_dup = 0;
    for (int i = 0; i < 8192; i += 256) {
        for (int j = i + 256; j < 8192; j += 256) {
            if (memcmp(huge + i, huge + j, 256) == 0) chunk_dup++;
        }
    }
    CHECK_EQ(chunk_dup, 0, "32 个分块两两不同（分块循环没把同一段写 32 次）");

    int zero_chunks = 0;
    for (int i = 0; i < 8192; i += 256) {
        int all_zero = 1;
        for (int j = 0; j < 256; j++) {
            if (huge[i + j] != 0) { all_zero = 0; break; }
        }
        if (all_zero) zero_chunks++;
    }
    CHECK_EQ(zero_chunks, 0, "分块边界之后依然有数据（没有提前停止填充）");

    TEST_END();
}
