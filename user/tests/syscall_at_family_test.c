/* at 家族测试：openat / fstatat / unlinkat / mkdirat / faccessat / fchmodat
 *
 * 重点验证三条容易写错的 POSIX 规则：
 *   1. path 是绝对路径时 **dirfd 被完全忽略**（哪怕 dirfd = -1）
 *   2. dirfd 必须是**目录**；普通文件 fd 当 dirfd 要失败（ENOTDIR）
 *   3. AT_EACCESS 让 faccessat 用 euid 而不是 ruid 检查——这是它和 access 的分野
 *
 * 另外验证 at 版本与老版本行为一致（同一套底层实现），以及相对路径可以带
 * 子目录（"sub/b.txt"）与 ".."。
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
    const char *dir = "/tmp/atdir";
    char buf[32];
    struct stat st;

    puts("=== at 家族测试 (openat/fstatat/unlinkat/mkdirat/faccessat/fchmodat) ===\n");

    /* ---------- 0. 准备目录 ---------- */
    remove_recursive(dir);
    check(mkdir(dir, 0755) == 0, "mkdir /tmp/atdir");

    int dirfd = open(dir, O_RDONLY);
    check(dirfd >= 0, "open(dir, O_RDONLY) 得到目录 fd");
    check(fstat(dirfd, &st) == 0 && S_ISDIR(st.st_mode), "fstat(dirfd) 是目录");

    /* ---------- 1. openat 相对 dirfd ---------- */
    int fd = openat(dirfd, "a.txt", O_CREAT | O_RDWR, 0644);
    check(fd >= 0, "openat(dirfd, \"a.txt\", O_CREAT|O_RDWR, 0644)");
    check(write(fd, "hello", 5) == 5, "write 5 字节");
    close(fd);

    check(fstatat(dirfd, "a.txt", &st, 0) == 0, "fstatat(dirfd, \"a.txt\")");
    check(st.st_size == 5, "fstatat 读到 size == 5");

    /* ---------- 2. 绝对路径忽略 dirfd（POSIX 明确要求） ---------- */
    check(fstatat(AT_FDCWD, "/tmp/atdir/a.txt", &st, 0) == 0,
          "fstatat(AT_FDCWD, 绝对路径)");
    check(fstatat(-1, "/tmp/atdir/a.txt", &st, 0) == 0,
          "fstatat(-1, 绝对路径) —— dirfd 无效也必须成功");
    int f2 = openat(-1, "/tmp/atdir/a.txt", O_RDONLY);
    check(f2 >= 0, "openat(-1, 绝对路径) —— 同样忽略 dirfd");
    if (f2 >= 0) {
        int n = (int)read(f2, buf, sizeof(buf) - 1);
        if (n > 0) buf[n] = 0;
        check(n == 5 && buf[0] == 'h' && buf[4] == 'o', "读回 \"hello\"");
        close(f2);
    }

    /* ---------- 3. 相对路径的子目录与 .. ---------- */
    check(mkdirat(dirfd, "sub", 0700) == 0, "mkdirat(dirfd, \"sub\", 0700)");
    check(fstatat(dirfd, "sub", &st, 0) == 0 && (st.st_mode & 0777) == 0700,
          "mkdirat 的 mode 真正生效(0700)");

    int fd2 = openat(dirfd, "sub/b.txt", O_CREAT | O_WRONLY, 0600);
    check(fd2 >= 0, "openat(dirfd, \"sub/b.txt\", O_CREAT) 跨一层目录");
    if (fd2 >= 0) close(fd2);

    check(fstatat(dirfd, "..", &st, 0) == 0 && S_ISDIR(st.st_mode),
          "fstatat(dirfd, \"..\") 解析到 /tmp");

    /* ---------- 4. 错误路径 ---------- */
    int regfd = openat(dirfd, "a.txt", O_RDONLY);
    check(regfd >= 0, "普通文件也能被 openat 相对打开");
    check(openat(regfd, "x", O_RDONLY) == -1,
          "用普通文件 fd 当 dirfd 必须失败(ENOTDIR)");
    if (regfd >= 0) close(regfd);

    check(openat(-1, "rel.txt", O_RDONLY) == -1, "dirfd=-1 + 相对路径 必须失败");
    check(openat(9999, "rel.txt", O_RDONLY) == -1, "dirfd 不存在 必须失败");

    /* 目录非空时 unlinkat(AT_REMOVEDIR) 要失败 */
    check(unlinkat(dirfd, "sub", 0) == -1, "unlinkat(dirfd, \"sub\", 0) 对目录失败");
    check(unlinkat(dirfd, "sub", AT_REMOVEDIR) == -1,
          "非空目录 unlinkat(AT_REMOVEDIR) 失败");
    check(unlinkat(dirfd, "sub", 0x400) == -1, "未定义的 unlinkat flag 被拒绝");

    /* ---------- 5. fchmodat ---------- */
    check(fchmodat(dirfd, "a.txt", 0600, 0) == 0, "fchmodat(dirfd, \"a.txt\", 0600)");
    check(fstatat(dirfd, "a.txt", &st, 0) == 0 && (st.st_mode & 0777) == 0600,
          "fchmodat 生效(mode == 0600)");
    check(fchmodat(dirfd, "a.txt", 0600, AT_SYMLINK_NOFOLLOW) == -1,
          "fchmodat 带 AT_SYMLINK_NOFOLLOW 明确拒绝(未实现)");

    /* ---------- 6. faccessat 与 AT_EACCESS ---------- */
    check(faccessat(dirfd, "a.txt", F_OK, 0) == 0, "faccessat(F_OK) 存在");
    check(faccessat(dirfd, "nope.txt", F_OK, 0) == -1, "faccessat(F_OK) 不存在 -> -1");
    check(faccessat(dirfd, "a.txt", R_OK | W_OK, 0) == 0, "root: faccessat(R|W) 特权绕过");
    check(faccessat(dirfd, "a.txt", F_OK, AT_SYMLINK_NOFOLLOW) == -1,
          "faccessat 带 AT_SYMLINK_NOFOLLOW 明确拒绝(未实现)");
    check(fstatat(dirfd, "a.txt", &st, 0x800) == -1, "fstatat 未知 flag 被拒绝");

    /* 造一个"属主是 1000、other 只读"的文件，然后让本进程变成 (ruid=1000, euid=0)：
     * 默认 faccessat 按 ruid(1000) 判 → 属主/other 里都无 w；AT_EACCESS 按
     * euid(0) 判 → 特权绕过。同一文件、同一 amode，结论相反。 */
    check(chmod(dir, 0755) == 0, "chmod(dir, 0755) 让 1000 能穿越");
    check(chown("/tmp/atdir/a.txt", 1000, 1000) == 0, "chown(a.txt, 1000, 1000)");
    check(chmod("/tmp/atdir/a.txt", 0604) == 0, "chmod(a.txt, 0604) other 只读");
    check(setreuid(1000, 0) == 0, "setreuid(1000, 0) —— ruid=1000, euid=0");
    check(getuid() == 1000 && geteuid() == 0, "身份确认 ruid=1000 euid=0");

    check(faccessat(AT_FDCWD, "/tmp/atdir/a.txt", W_OK, 0) == -1,
          "faccessat(W_OK, 无 AT_EACCESS) 按 ruid 判 -> 拒绝");
    check(faccessat(AT_FDCWD, "/tmp/atdir/a.txt", W_OK, AT_EACCESS) == 0,
          "faccessat(W_OK, AT_EACCESS) 按 euid=0 判 -> 放行");
    check(setreuid(0, 0) == 0, "setreuid(0, 0) 恢复 root");

    /* ---------- 7. 清理 ---------- */
    check(unlinkat(dirfd, "sub/b.txt", 0) == 0, "unlinkat 删除子文件");
    check(unlinkat(dirfd, "sub", AT_REMOVEDIR) == 0, "空目录 unlinkat(AT_REMOVEDIR) 成功");
    check(unlinkat(dirfd, "a.txt", 0) == 0, "unlinkat 删除 a.txt");
    check(fstatat(dirfd, "a.txt", &st, 0) == -1, "删除后 fstatat -> -1");
    check(unlinkat(AT_FDCWD, "/tmp/atdir", AT_REMOVEDIR) == 0,
          "unlinkat(AT_FDCWD, 绝对路径, AT_REMOVEDIR)");
    close(dirfd);

    puts("\n");
    if (fails) {
        printf("%d 项失败\n", fails);
        return 1;
    }
    puts("all passed\n");
    return 0;
}
