// 文件权限系统(Layer 2)验证程序。
//
// 在 init/root(euid==0) 下运行, 覆盖:
//   1. stat 能正确返回文件属主/属组/权限位(mode)
//   2. chmod 往返(root 可改, 且 setuid 位可被置位)
//   3. chown 往返(root 可改属主/属组)
//   4. 降权后(euid!=0)无法读 root 的 0600 文件(访问检查)
//   5. 降权后无法 chmod/chown 非自己所有的文件
//   6. setuid 可执行位: exec 一个属主为 100 且带 S_ISUID 的文件,
//      euid 应被提升为 100(用 uid/euid 组合守卫防止递归 exec)。
//
// 注意: C 的八进制字面量用 0 前缀(0644), 不是 0o644。
// 所有测试都在 /tmp(tmpfs, 可写) 下进行, 因为 ext4 是只读的。

#include "user.h"

static int g_fails = 0;

static int check(const char *tag, isize got, isize want) {
    printf("%s => got=%d want=%d ", tag, (int)got, (int)want);
    if (got == want) {
        printf("[OK]\n");
        return 0;
    }
    printf("[FAIL]\n");
    return 1;
}

/* 把一个文件整体拷贝到另一个路径(裸机无 malloc, 用栈缓冲分块读写)。 */
static int copy_file(const char *src, const char *dst) {
    int in = open(src, O_RDONLY);
    if (in < 0) return -1;
    int out = open_create(dst, O_WRONLY);
    if (out < 0) { close(in); return -1; }
    char buf[512];
    for (;;) {
        isize n = read(in, buf, sizeof(buf));
        if (n < 0) { close(in); close(out); return -1; }
        if (n == 0) break;
        isize w = write(out, buf, (usize)n);
        if (w != n) { close(in); close(out); return -1; }
    }
    close(in);
    close(out);
    return 0;
}

int main(void) {
    /*
     * 哨兵守卫: 本程序会被自己 exec 出来做一次 setuid 提权验证。
     * 被 exec 的实例: 真实 uid 仍为 0(内核只改有效 uid), 但 euid 被提升为
     * 文件属主 100 -> getuid()==0 && geteuid()==100。这恰好区别于第 4/5 步
     * 里 fork 出来、用 setuid(100) 把真实 uid 也改成 100 的子进程。
     * 命中守卫即短路返回, 避免无限递归 exec。
     */
    if (getuid() == 0 && geteuid() == 100) {
        printf("[elevated] real uid=%d euid=%d (期望 0/100) -> setuid 提权成功 [OK]\n",
               (int)getuid(), (int)geteuid());
        return 0;
    }

    printf("=== Layer2 权限系统测试 ===\n");

    /* 切到 /tmp, 在可写的 tmpfs 上做实验 */
    if (chdir("/tmp") != 0) {
        printf("[FATAL] chdir /tmp failed\n");
        return 1;
    }

    /* ---------- 1. stat 基础字段 ---------- */
    printf("\n=== 1. stat 属主/权限位 ===\n");
    int fd = open_create("perm_t", O_WRONLY);
    if (fd < 0) { printf("[FATAL] create perm_t failed\n"); return 1; }
    write(fd, "hello", 5);
    close(fd);

    struct stat st;
    if (stat("perm_t", &st) != 0) { printf("[FATAL] stat perm_t failed\n"); return 1; }
    printf("  file_type=%d(1=FILE) uid=%d gid=%d mode=0%o size=%d\n",
           stat_type_of(st.st_mode), st.st_uid, st.st_gid, st.st_mode & 07777, (int)st.st_size);
    g_fails += check("[1] file_type", stat_type_of(st.st_mode), STAT_TYPE_FILE);
    g_fails += check("[1] uid(root)", st.st_uid, 0);
    g_fails += check("[1] gid(root)", st.st_gid, 0);
    g_fails += check("[1] mode 0644", st.st_mode & 07777, 0644);

    /* ---------- 2. chmod 往返 ---------- */
    printf("\n=== 2. chmod 往返(root) ===\n");
    g_fails += check("[2] chmod 0600", chmod("perm_t", 0600), 0);
    stat("perm_t", &st);
    g_fails += check("[2] mode now 0600", st.st_mode & 07777, 0600);
    g_fails += check("[2] chmod 0644 back", chmod("perm_t", 0644), 0);
    stat("perm_t", &st);
    g_fails += check("[2] mode back 0644", st.st_mode & 07777, 0644);

    /* ---------- 3. chown 往返 ---------- */
    printf("\n=== 3. chown 往返(root) ===\n");
    g_fails += check("[3] chown 100:100", chown("perm_t", 100, 100), 0);
    stat("perm_t", &st);
    g_fails += check("[3] uid now 100", st.st_uid, 100);
    g_fails += check("[3] gid now 100", st.st_gid, 100);
    g_fails += check("[3] chown 0:0 back", chown("perm_t", 0, 0), 0);
    stat("perm_t", &st);
    g_fails += check("[3] uid back 0", st.st_uid, 0);
    g_fails += check("[3] gid back 0", st.st_gid, 0);

    /* 把 perm_t 设成 root 所有的 0600, 用作后面的访问检查目标 */
    chmod("perm_t", 0600);

    /* ---------- 4/5. 降权后的访问检查 ---------- */
    printf("\n=== 4/5. 降权后访问检查(euid=100) ===\n");
    isize child = fork();
    if (child == 0) {
        /* 子进程降到非特权 euid=100 */
        if (setuid(100) != 0) { printf("[child] setuid(100) failed\n"); exit(1); }
        printf("[child] euid=%d(期望 100)\n", (int)geteuid());

        /* 4a. 读 root 的 0600 文件必须失败 */
        int rf = open("perm_t", O_RDONLY);
        if (rf >= 0) { close(rf); printf("[child] open 0600 竟成功 -> 错误\n"); exit(1); }
        printf("[child] open(root 0600) 被拒绝 [OK]\n");

        /* 5a. 非属主 chmod 必须失败 */
        isize c = chmod("perm_t", 0644);
        if (c == 0) { printf("[child] chmod 竟成功 -> 错误\n"); exit(1); }
        printf("[child] chmod(非属主) 被拒绝 [OK]\n");

        /* 5b. 非 root chown 必须失败 */
        isize o = chown("perm_t", 200, 200);
        if (o == 0) { printf("[child] chown 竟成功 -> 错误\n"); exit(1); }
        printf("[child] chown(非 root) 被拒绝 [OK]\n");

        printf("[child] exit 0\n");
        exit(0);
    } else if (child > 0) {
        int code = -1;
        waitpid(child, &code, 0);
        if (code != 0) { printf("[4/5] child 报告失败 code=%d\n", code); g_fails++; }
        else printf("[4/5] 降权访问检查全部符合预期 [OK]\n");
    } else {
        printf("[4/5] fork error\n");
        g_fails++;
    }

    /* root 自己应当能读 0600(超级用户绕过) */
    {
        int rf = open("perm_t", O_RDONLY);
        if (rf >= 0) { close(rf); printf("[root] 读 0600 成功(绕过) [OK]\n"); }
        else { printf("[root] 读 0600 失败 -> 错误\n"); g_fails++; }
    }

    /* ---------- 6. setuid 可执行位(exec 提权) ---------- */
    printf("\n=== 6. setuid 可执行位(exec 提权到 euid=100) ===\n");
    /*
     * 思路: 把本程序自身(/samples/xx_perm)拷到 /tmp/xx_perm_su,
     * chown 100:100 并 chmod 04755(置 S_ISUID), 然后 fork 一个子进程
     * 以 root 身份 exec 它。内核应在 exec 时把 euid 提升为文件属主(100)。
     * 被 exec 出来的实例在 main 顶部看到 uid=0 && euid=100 即短路返回,
     * 避免无限递归。
     */
    if (copy_file("/samples/xx_perm", "/tmp/xx_perm_su") != 0) {
        printf("[6] 复制自身失败, 跳过 setuid exec 测试\n");
    } else if (chown("/tmp/xx_perm_su", 100, 100) != 0) {
        printf("[6] chown su 失败, 跳过\n");
    } else if (chmod("/tmp/xx_perm_su", 04755) != 0) {
        printf("[6] chmod su+S_ISUID 失败, 跳过\n");
    } else {
        /* 确认 setuid 位确实被置上 */
        stat("/tmp/xx_perm_su", &st);
        if ((st.st_mode & 04000) != 04000) {
            printf("[6] S_ISUID 未置上(mode=0%o), 跳过\n", st.st_mode & 07777);
        } else {
            isize pc = fork();
            if (pc == 0) {
                exec("/tmp/xx_perm_su");
                printf("[probe] exec 失败\n");
                exit(1);
            } else if (pc > 0) {
                int code = -1;
                waitpid(pc, &code, 0);
                if (code != 0) { printf("[6] setuid exec 提权验证失败 code=%d\n", code); g_fails++; }
                else printf("[6] setuid exec 提权验证 [OK]\n");
            } else {
                printf("[6] fork error\n");
                g_fails++;
            }
        }
    }
    unlink("/tmp/xx_perm_su");

    /* ---------- 汇总 ---------- */
    printf("\n=== SUMMARY ===\n");
    if (g_fails == 0) printf("[ALL PASS]\n");
    else printf("[SOME FAIL] (%d)\n", g_fails);
    return g_fails;
}
