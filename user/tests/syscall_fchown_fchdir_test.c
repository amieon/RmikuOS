/* fchown / fchdir 测试
 *
 * fchdir 的验证重点：切过去之后 cwd 真的变了（getcwd + 相对路径 openat 双重确认），
 * 以及"非目录 fd 必须失败"。另测一个按路径实现才会出现的边界：
 * 目录被删掉之后，手里那个 fd 再 fchdir 应当失败（不能把 cwd 设成悬空路径）。
 *
 * fchown 的验证重点：fd 版与路径版看到同一份元数据（改完 fstat 和 stat 一致）；
 * 没有 inode 的 fd（pipe / 终端）失败；非 root 一律失败（本内核 chown 是 root-only）。
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
    const char *dir = "/tmp/fdtest";
    const char *file = "/tmp/fdtest/a.bin";
    char cwd[128];
    struct stat st;

    puts("=== fchown / fchdir 测试 ===\n");

    /* ---------- 0. 准备 ---------- */
    remove_recursive(dir);
    check(mkdir(dir, 0755) == 0, "mkdir /tmp/fdtest");
    int f = open(file, O_CREAT | O_RDWR, 0644);
    check(f >= 0, "创建 /tmp/fdtest/a.bin");

    int dfd = open(dir, O_RDONLY);
    check(dfd >= 0, "open(dir, O_RDONLY) 得到目录 fd");

    /* ---------- 1. fchdir 正常路径 ---------- */
    check(fchdir(dfd) == 0, "fchdir(目录 fd) == 0");
    check(getcwd(cwd, sizeof(cwd)) != (char *)0 && strcmp(cwd, dir) == 0,
          "getcwd 显示已切到 /tmp/fdtest");

    int rel = openat(AT_FDCWD, "a.bin", O_RDONLY);
    check(rel >= 0, "切换后 openat(AT_FDCWD, \"a.bin\") 能解析");
    if (rel >= 0) close(rel);

    /* ---------- 2. fchdir 错误路径 ---------- */
    check(fchdir(f) == -1, "对普通文件 fd fchdir 必须失败(ENOTDIR)");
    check(fchdir(9999) == -1, "对不存在的 fd fchdir 失败");

    int pfd[2];
    if (pipe(pfd) == 0) {
        check(fchdir(pfd[0]) == -1, "对 pipe 的 fd fchdir 失败");
        close(pfd[0]);
        close(pfd[1]);
    }

    /* ---------- 3. 目录被删后再 fchdir ---------- */
    check(mkdir("/tmp/fdtest/gone", 0755) == 0, "mkdir /tmp/fdtest/gone");
    int gfd = open("/tmp/fdtest/gone", O_RDONLY);
    check(gfd >= 0, "open 这个目录留一个 fd");
    check(rmdir("/tmp/fdtest/gone") == 0, "rmdir 掉它(fd 仍活着)");
    check(fchdir(gfd) == -1,
          "目录已删除 -> fchdir 失败(按路径实现的选择:不留悬空 cwd)");
    if (gfd >= 0) close(gfd);

    /* 回到 / 再做后面的路径断言 */
    check(chdir("/") == 0, "chdir(\"/\") 复位");
    check(getcwd(cwd, sizeof(cwd)) != (char *)0 && strcmp(cwd, "/") == 0,
          "cwd 已回到 /");

    /* ---------- 4. fchown 正常路径 ---------- */
    check(fchown(f, 1234, 5678) == 0, "root: fchown(fd, 1234, 5678)");
    check(fstat(f, &st) == 0 && st.st_uid == 1234 && st.st_gid == 5678,
          "fstat 看到 uid=1234 gid=5678");
    check(stat(file, &st) == 0 && st.st_uid == 1234 && st.st_gid == 5678,
          "路径 stat 看到同一份元数据(fd 版与路径版同源)");

    /* fchmod 也必须能被**同一个 fd** 的 fstat 立刻看到。
     * 这是同一个"权限被缓存在 File 里"的陷阱：tmpfs 的 TmpfsFile 原来在 open 时
     * 拷了一份 mode/uid/gid，fstat 读的是旧副本；test 102 当时用路径 stat 验证，
     * 正好绕过了它，所以直到 fchown 才暴露。 */
    check(fchmod(f, 0700) == 0, "fchmod(fd, 0700)");
    check(fstat(f, &st) == 0 && (st.st_mode & 0777) == 0700,
          "同一 fd 的 fstat 立刻看到新 mode(权限没有被缓存副本)");

    /* ---------- 5. fchown 错误路径 ---------- */
    check(fchown(9999, 0, 0) == -1, "对不存在的 fd fchown 失败");
    check(fchown(0, 0, 0) == -1, "对标准输入(终端,无 inode) fchown 失败");
    if (pipe(pfd) == 0) {
        check(fchown(pfd[1], 0, 0) == -1, "对 pipe fchown 失败(无 inode)");
        close(pfd[0]);
        close(pfd[1]);
    }

    /* 只读 rootfs 上的文件：ReadOnlyMemFile 定位不到 inode */
    int rf = open("/etc/passwd", O_RDONLY);
    if (rf >= 0) {
        check(fchown(rf, 0, 0) == -1, "只读 rootfs 的 fd fchown 失败");
        close(rf);
    }

    /* ---------- 6. 非 root 不能 fchown ----------
     * 用可逆的 seteuid 而不是 setreuid(1000,1000)：后者是单向门，切完就回不来，
     * 后面的清理会因为没有 root 权限而失败。seteuid 只动有效身份，ruid 仍是 0，
     * 所以 seteuid(0) 还能切回来（compute_seteuid 允许 euid==ruid 的切换）。 */
    check(seteuid(1000) == 0, "seteuid(1000)");
    check(geteuid() == 1000, "euid 现在是 1000");
    check(fchown(f, 1000, 1000) == -1,
          "非 root 即使想把文件改成自己也不行(本内核 chown 是 root-only)");
    check(seteuid(0) == 0, "seteuid(0) 恢复 root");
    check(geteuid() == 0, "euid 已回到 0");

    /* ---------- 7. 清理 ---------- */
    if (f >= 0) close(f);
    if (dfd >= 0) close(dfd);
    remove_recursive(dir);

    puts("\n");
    if (fails) {
        printf("%d 项失败\n", fails);
        return 1;
    }
    puts("all passed\n");
    return 0;
}
