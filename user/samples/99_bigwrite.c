// 99_bigwrite.c — 大文件持续写入器（可写 ext4 崩溃一致性压测用）
//
// 用法: bigwrite [路径] [总MB] [sync]
//   bigwrite                    → /home/root/big.bin, 16MB, 不主动 sync
//   bigwrite /home/root/b.bin 8 → 指定路径和大小
//   bigwrite /home/root/b.bin 8 sync → 每写满 1MB 调一次 fsync（对照实验用）
//
// 两种实验姿势:
//   实验B（崩溃窗口）: 默认模式跑起来，看到"已写 N MB"滚动时 kill -9 QEMU
//                      → 重启后 ls 看文件大小 = 真正落盘的量（应小于写入量）
//   实验A（持久化）  : 加 sync 参数，写完/fsync 后再 kill
//                      → 重启后文件完整
//
// 数据水印: 每个 4KB 块的头部 4 字节写块号，其余填固定图案——
// 将来想精确验证"写到哪里断的"可以逐块读回比对块号。

#include "user.h"

#define BLOCK_SIZE 4096
#define BLOCKS_PER_MB 256   // 4096 * 256 = 1MB

static char buf[BLOCK_SIZE];

static void fill_block(int block_no) {
    // 头 4 字节: 块号水印；其余: 随块号变化的填充图案
    int *stamp = (int *)buf;
    *stamp = block_no;
    char filler = (char)(0xA0 | (block_no & 0x0F));
    for (int i = 4; i < BLOCK_SIZE; i++) {
        buf[i] = filler;
    }
}

static int parse_positive_int(const char *s, int fallback) {
    int x = 0;
    int i = 0;
    if (!s || !s[0]) return fallback;
    while (s[i]) {
        if (s[i] < '0' || s[i] > '9') return fallback;
        x = x * 10 + (s[i] - '0');
        i++;
    }
    return x > 0 ? x : fallback;
}

int main(int argc, char *argv[]) {
    const char *path = (argc > 1 && argv[1][0]) ? argv[1] : "/home/root/big.bin";
    int total_mb = (argc > 2) ? parse_positive_int(argv[2], 16) : 16;
    int do_sync = (argc > 3 && argv[3][0] == 's');   // 传 "sync" 开启

    puts("[bigwrite] 目标: ");
    puts(path);
    puts("  大小: ");
    printf("%d", total_mb);
    puts(" MB  模式: ");
    puts(do_sync ? "每MB fsync\n" : "不主动sync(崩溃窗口测试)\n");

    // O_CREAT 时按惯例必须传 mode
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        puts("[bigwrite] open 失败: ");
        puts(path);
        puts("  (目录存在吗? 先 mkdir /home/root)\n");
        exit(1);
    }

    for (int mb = 0; mb < total_mb; mb++) {
        for (int b = 0; b < BLOCKS_PER_MB; b++) {
            int idx = mb * BLOCKS_PER_MB + b;
            fill_block(idx);
            isize n = write(fd, buf, BLOCK_SIZE);
            if (n != BLOCK_SIZE) {
                puts("[bigwrite] write 失败 @ 块 ");
                printf("%d", idx);
                puts(" (盘满?)\n");
                close(fd);
                exit(1);
            }
        }
        // 进度提示: kill -9 就在滚动时下手
        puts("[bigwrite] 已写 ");
        printf("%d", mb + 1);
        puts(" / ");
        printf("%d", total_mb);
        puts(" MB\n");

        if (do_sync) {
            if (fsync(fd) == 0) {
                puts("[bigwrite]   fsync ok\n");
            } else {
                puts("[bigwrite]   fsync 失败!\n");
            }
        }
    }

    close(fd);
    puts("[bigwrite] 完成\n");
    exit(0);
    return 0;
}
