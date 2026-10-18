/* struct stat 字段完整性测试
 *
 * 为什么单独测这个：st_dev / st_ino / st_nlink 在本内核里**没有真实语义**
 * （没有设备号、没有 inode 号、不计硬链接），但 POSIX 程序会直接读它们——
 * git 的 index 就存 st_dev/st_ino 做"inode 是否变了"的启发式，源码里写的是
 * st.st_ino。字段缺失是**编译错误**，不是运行期问题，所以必须有测试钉住：
 *
 *   1. 三个字段存在（能编过就是第一层保证）
 *   2. 取值是文档承诺的常量（0 / 0 / 目录 2 其余 1）
 *   3. 不破坏原有字段（mode/size/uid/gid 仍正确）
 */
#include "user.h"

static int fails = 0;

static void check(int cond, const char *msg) {
    if (cond) {
        puts("  OK   \n");
    } else {
        puts("  FAIL \n");
        puts("         ");
        puts(msg);
        puts("\n");
        fails++;
    }
}

int main() {
    const char *d = "/tmp/stfields";
    const char *f = "/tmp/stfields/a.bin";
    const char *l = "/tmp/stfields/link";
    struct stat st;

    puts("=== struct stat 字段完整性测试 ===\n");

    remove_recursive(d);
    check(mkdir(d, 0755) == 0, "mkdir /tmp/stfields");

    /* ---------- 1. 普通文件 ---------- */
    int fd = open(f, O_CREAT | O_RDWR, 0644);
    check(fd >= 0, "创建并打开 a.bin");
    if (fd >= 0) write(fd, "12345", 5);

    check(stat(f, &st) == 0, "stat 普通文件");
    check(st.st_size == 5, "st_size 正确(原有字段未受影响)");
    check(st.st_nlink == 1, "普通文件 st_nlink == 1");
    check(st.st_dev == 0, "普通文件 st_dev == 0");
    check(st.st_ino == 0, "普通文件 st_ino == 0");
    check(st.st_rdev == 0, "普通文件 st_rdev == 0");

    /* fstat 走的是同一个转换函数，顺手验一遍 */
    if (fd >= 0) {
        struct stat fs_st;
        check(fstat(fd, &fs_st) == 0 && fs_st.st_nlink == 1 && fs_st.st_size == 5,
              "fstat 的字段与 stat 一致");
        close(fd);
    }

    /* ---------- 2. 目录 ---------- */
    check(stat(d, &st) == 0 && S_ISDIR(st.st_mode), "stat 目录");
    check(st.st_nlink == 2, "目录 st_nlink == 2(POSIX 惯例: 自身 + \".\")");

    /* ---------- 3. 符号链接 ---------- */
    check(symlink(f, l) == 0, "创建符号链接");
    check(lstat(l, &st) == 0 && S_ISLNK(st.st_mode), "lstat 看到的是链接本身");
    check(st.st_nlink == 1, "符号链接 st_nlink == 1");

    /* ---------- 4. 清理 ---------- */
    unlink(l);
    remove_recursive(d);

    puts("\n");
    if (fails) {
        printf("%d 项失败\n", fails);
        return 1;
    }
    puts("all passed\n");
    return 0;
}
