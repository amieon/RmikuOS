/* renameat / linkat 测试
 *
 * 核心不是"能改名"，而是**两边的 dirfd 可以不同、且能任意与 AT_FDCWD 混搭**。
 * 覆盖：
 *   1. dirfd → dirfd（两个不同目录之间）
 *   2. dirfd → AT_FDCWD（绝对路径）
 *   3. AT_FDCWD → dirfd（反方向）
 *   4. 无效 dirfd 失败
 *   5. linkat 建出的硬链接与源是**同一个 inode**——用"改一个、读另一个"来证明
 *   6. linkat 的 flags != 0 明确拒绝（AT_SYMLINK_FOLLOW 未实现）
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
    const char *root = "/tmp/rat";
    const char *d1p = "/tmp/rat/d1";
    const char *d2p = "/tmp/rat/d2";
    char buf[16];
    struct stat st;

    puts("=== renameat / linkat 测试 ===\n");

    /* ---------- 0. 准备 ---------- */
    remove_recursive(root);
    check(mkdir(root, 0755) == 0, "mkdir /tmp/rat");
    check(mkdir(d1p, 0755) == 0, "mkdir /tmp/rat/d1");
    check(mkdir(d2p, 0755) == 0, "mkdir /tmp/rat/d2");

    int fd1 = open(d1p, O_RDONLY);
    int fd2 = open(d2p, O_RDONLY);
    check(fd1 >= 0 && fd2 >= 0, "打开两个目录 fd");

    int f = open("/tmp/rat/d1/f.txt", O_CREAT | O_RDWR, 0644);
    check(f >= 0 && write(f, "hello", 5) == 5, "创建 d1/f.txt 写入 hello");
    if (f >= 0) close(f);

    /* ---------- 1. dirfd → dirfd ---------- */
    check(renameat(fd1, "f.txt", fd2, "g.txt") == 0,
          "renameat(fd1, \"f.txt\", fd2, \"g.txt\")");
    check(fstatat(fd1, "f.txt", &st, 0) == -1, "源目录里已没有 f.txt");
    check(fstatat(fd2, "g.txt", &st, 0) == 0 && st.st_size == 5,
          "目标目录里有 g.txt 且内容长度不变");

    /* ---------- 2. dirfd → AT_FDCWD ---------- */
    check(renameat(fd2, "g.txt", AT_FDCWD, "/tmp/rat/h.txt") == 0,
          "renameat(fd2, \"g.txt\", AT_FDCWD, \"/tmp/rat/h.txt\")");
    check(fstatat(fd2, "g.txt", &st, 0) == -1, "d2 里 g.txt 已消失");
    check(stat("/tmp/rat/h.txt", &st) == 0, "/tmp/rat/h.txt 存在");

    /* ---------- 3. AT_FDCWD → dirfd ---------- */
    check(renameat(AT_FDCWD, "/tmp/rat/h.txt", fd1, "back.txt") == 0,
          "renameat(AT_FDCWD, 绝对路径, fd1, \"back.txt\")");
    check(fstatat(fd1, "back.txt", &st, 0) == 0, "d1 里出现 back.txt");

    /* ---------- 4. 错误路径 ---------- */
    check(renameat(fd1, "back.txt", 9999, "x.txt") == -1, "目标 dirfd 无效 -> -1");
    check(renameat(9999, "back.txt", fd1, "x.txt") == -1, "源 dirfd 无效 -> -1");
    check(renameat(fd1, "nope.txt", fd2, "x.txt") == -1, "源不存在 -> -1");

    /* ---------- 5. linkat：硬链接是同一个 inode ---------- */
    check(linkat(AT_FDCWD, "/tmp/rat/d1/back.txt", AT_FDCWD, "/tmp/rat/hard.txt", 0) == 0,
          "linkat(绝对路径 -> 绝对路径)");
    check(fstatat(fd1, "back.txt", &st, 0) == 0 && stat("/tmp/rat/hard.txt", &st) == 0,
          "两个名字都能 stat 到");

    /* 通过硬链接写，从原名字读 —— 证明是同一份数据而不是拷贝 */
    int w = open("/tmp/rat/hard.txt", O_WRONLY);
    check(w >= 0 && write(w, "XY", 2) == 2, "通过 hard.txt 覆写前 2 字节");
    if (w >= 0) close(w);

    int r = openat(fd1, "back.txt", O_RDONLY);
    check(r >= 0, "从原名字读回");
    if (r >= 0) {
        int n = (int)read(r, buf, sizeof(buf));
        check(n == 5 && buf[0] == 'X' && buf[1] == 'Y' && buf[4] == 'o',
              "读到的内容已被硬链接那侧改掉(同一 inode)");
        close(r);
    }

    /* linkat 两侧都用 dirfd */
    check(linkat(fd1, "back.txt", fd2, "linked.txt", 0) == 0,
          "linkat(fd1, \"back.txt\", fd2, \"linked.txt\", 0)");
    check(fstatat(fd2, "linked.txt", &st, 0) == 0, "d2 里出现 linked.txt");

    /* ---------- 6. 未实现的 flag 明确拒绝 ---------- */
    check(linkat(fd1, "back.txt", fd2, "x2.txt", AT_SYMLINK_FOLLOW) == -1,
          "linkat 带非零 flags 明确拒绝(AT_SYMLINK_FOLLOW 未实现)");
    check(fstatat(fd2, "x2.txt", &st, 0) == -1, "被拒绝时没有留下半个链接");
    check(linkat(fd1, "nope.txt", fd2, "x3.txt", 0) == -1, "linkat 源不存在 -> -1");

    /* ---------- 7. 清理 ---------- */
    if (fd1 >= 0) close(fd1);
    if (fd2 >= 0) close(fd2);
    chdir("/");
    remove_recursive(root);

    puts("\n");
    if (fails) {
        printf("%d 项失败\n", fails);
        return 1;
    }
    puts("all passed\n");
    return 0;
}
