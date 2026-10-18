/* O_EXCL 与 umask 测试
 *
 * 这两个不是"锦上添花"，是 git 能安全工作的前提：
 *   - O_EXCL：git 的每个写操作都先 open(.git/index.lock, O_CREAT|O_EXCL)。
 *     不认这个位的话，锁形同虚设（拿到一个已存在的文件还当成功），
 *     并发或残留锁会静默损坏索引。
 *   - umask：git checkout 传 0666/0777，指望内核剥掉掩码位。
 *     不生效的话工作区文件全是 0666/0777（世界可写）。
 *
 * 注意：open 的 mode 参数**只在新建时生效**。所以每条权限断言都必须用
 * 全新的文件名，否则测的是上次留下的权限位（会假通过）。
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

/* 专用于权限位：失败时把期望/实际都打出来，省得再来回猜 */
static void check_mode(const char *path, int expect, const char *msg) {
    struct stat st;
    if (stat(path, &st) != 0) {
        printf("  FAIL  %s (stat 失败)\n", msg);
        fails++;
        return;
    }
    int got = (int)(st.st_mode & 0777);
    if (got == expect) {
        puts("  OK   \n");
        return;
    }
    printf("  FAIL  %s (期望 0%o, 实际 0%o)\n", msg, expect, got);
    fails++;
}

int main() {
    const char *d = "/tmp/excl";
    const char *f1 = "/tmp/excl/a.bin";
    const char *f2 = "/tmp/excl/b.bin";
    struct stat st;

    puts("=== O_EXCL / umask 测试 ===\n");

    remove_recursive(d);
    check(mkdir(d, 0755) == 0, "mkdir /tmp/excl");

    /* ---------- 1. O_EXCL ---------- */
    int fd = open(f1, O_CREAT | O_RDWR, 0644);
    check(fd >= 0, "第一次 open(O_CREAT) 成功");
    if (fd >= 0) close(fd);

    check(open(f1, O_CREAT | O_EXCL | O_RDWR, 0644) == -1,
          "对已存在文件 O_CREAT|O_EXCL 必须失败");
    check(open(f2, O_CREAT | O_EXCL | O_RDWR, 0644) >= 0,
          "对新文件 O_CREAT|O_EXCL 成功");
    check(open(f2, O_CREAT | O_EXCL | O_RDWR, 0644) == -1,
          "紧接着再来一次必须失败(独占创建的全部意义)");

    /* O_EXCL 单独出现(没有 O_CREAT)：Linux 忽略它，照常打开 */
    check(open(f1, O_RDWR | O_EXCL, 0) >= 0,
          "O_EXCL 不带 O_CREAT 时被忽略(Linux 语义)");

    /* openat 也要走同一条判断 */
    int dfd = open(d, O_RDONLY);
    if (dfd >= 0) {
        check(openat(dfd, "a.bin", O_CREAT | O_EXCL | O_RDWR, 0644) == -1,
              "openat 的 O_CREAT|O_EXCL 同样生效");
        check(openat(dfd, "c.bin", O_CREAT | O_EXCL | O_RDWR, 0644) >= 0,
              "openat 建新文件成功");
        close(dfd);
    }

    /* ---------- 2. umask ---------- */
    mode_t old = umask(0);
    check(old == 022, "默认 umask 是 022");

    int g = open("/tmp/excl/m0.bin", O_CREAT | O_EXCL | O_RDWR, 0666);
    check(g >= 0, "umask=0 时新建文件");
    if (g >= 0) close(g);
    check_mode("/tmp/excl/m0.bin", 0666, "umask=0 → 新文件 0666 原样落盘");

    mode_t prev = umask(022);
    check(prev == 0, "umask(022) 返回旧值 0");

    /* 读回确认掩码真的存进去了（失败时这行能立刻定位问题） */
    mode_t rb = umask(0);
    umask(rb);
    printf("  [信息] umask 读回 = 0%o (应为 022)\n", (int)rb);
    check(rb == 022, "umask(022) 之后读回仍是 022");

    int g2 = open("/tmp/excl/m22.bin", O_CREAT | O_EXCL | O_WRONLY, 0666);
    check(g2 >= 0, "umask=022 时新建文件");
    if (g2 >= 0) close(g2);
    check_mode("/tmp/excl/m22.bin", 0644,
               "umask=022 → 新文件 0666 收窄成 0644(不能再是世界可写)");

    /* 目录：0777 应落成 0755；再用 0700 交叉验证 ——
     * 若 chmod 路径根本没生效，目录会停在新建默认值 0755，
     * 那样 0700 这条就会失败（0777 那条反而可能假通过）。 */
    int dfd2 = open(d, O_RDONLY);
    if (dfd2 >= 0) {
        check(mkdirat(dfd2, "sub", 0777) == 0, "mkdirat(0777)");
        check_mode("/tmp/excl/sub", 0755, "umask=022 → 目录 0777 收窄成 0755");

        check(mkdirat(dfd2, "sub2", 0700) == 0, "mkdirat(0700)");
        check_mode("/tmp/excl/sub2", 0700,
                   "mkdirat 的 mode 真的落盘(0700，不是默认 0755)");
        close(dfd2);
    } else {
        puts("  [跳过] 打不开 /tmp/excl，目录断言未执行\n");
    }

    /* 显式 chmod 不受 umask 影响（POSIX：umask 只管"创建"） */
    check(chmod("/tmp/excl/m22.bin", 0666) == 0, "显式 chmod 0666");
    check_mode("/tmp/excl/m22.bin", 0666, "chmod 不受 umask 影响");

    check(umask(old) == 022, "恢复原 umask");

    /* ---------- 3. 清理 ---------- */
    remove_recursive(d);

    puts("\n");
    if (fails) {
        printf("%d 项失败\n", fails);
        return 1;
    }
    puts("all passed\n");
    return 0;
}
