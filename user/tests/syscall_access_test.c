/* access() 测试 —— F_OK / R_OK / W_OK / X_OK + 真实 uid 语义
 *
 * access 与 open 的分野：open 按**有效**身份(euid)放行，access 按**真实**身份(ruid)。
 * 要测出这条分野，必须让 ruid 和 euid **不一致**——setreuid(1000, 0) 正是干这个的。
 *
 * 用例：
 *   1. root 身份：F_OK 存在/不存在、root 特权绕过一切权限位
 *   2. setreuid(1000, 0)：ruid=1000(非属主) 而 euid=0(root)
 *      → access 按 ruid 判，拒；open 按 euid 判，放行。**分野的铁证**
 *      （此步可逆：euid 还是 0，能 setreuid(0,0) 回 root）
 *   3. setreuid(1000, 1000)：纯非属主身份
 *      → other 位有 r 则 R_OK 过；无 w/x 则拒；0700 的 other 全空则全拒
 *      （单向门：切过去 ruid/euid/saved 全是 1000，再也回不了 root，所以放最后）
 *
 * 两个测试文件的属主都是 2000，与测试进程(1000)故意错开，确保走 other 分支。
 */
#include "user.h"

static int fails = 0;

static void check(int cond, const char *msg) {
    if (cond) {
        puts("  OK   ");
    } else {
        puts("  FAIL ");
        fails++;
    }
    puts(msg);
    put_char('\n');
}

int main() {
    const char *pa = "/tmp/at_a.bin";   /* 0604, 属主 2000 → other 有 r 无 w 无 x */
    const char *pb = "/tmp/at_b.bin";   /* 0700, 属主 2000 → other 全空 */

    puts("=== access() 测试 ===\n");

    /* ---------- 1. root 身份 ---------- */
    check(getuid() == 0, "当前是 root(uid=0)");

    check(access("/tmp", F_OK) == 0, "F_OK: /tmp 存在 -> 0");
    check(access("/no/such/file", F_OK) == -1, "F_OK: 不存在 -> -1");
    check(access(pa, F_OK) == -1, "F_OK: 尚未创建 -> -1");

    int fda = open(pa, O_CREAT | O_RDWR | O_TRUNC);
    int fdb = open(pb, O_CREAT | O_RDWR | O_TRUNC);
    check(fda >= 0 && fdb >= 0, "创建两个测试文件");
    if (fda >= 0) { write(fda, "abc", 3); close(fda); }
    if (fdb >= 0) { write(fdb, "abc", 3); close(fdb); }

    check(chmod(pa, 0604) == 0, "chmod(pa, 0604)");
    check(chown(pa, 2000, 2000) == 0, "chown(pa, 2000, 2000) —— 属主是 2000，不是本进程");
    check(chmod(pb, 0700) == 0, "chmod(pb, 0700)");
    check(chown(pb, 2000, 2000) == 0, "chown(pb, 2000, 2000)");

    /* root 特权：权限位再严也放行 */
    check(access(pa, R_OK) == 0, "root: pa R_OK -> 0(特权绕过)");
    check(access(pb, W_OK) == 0, "root: pb W_OK -> 0(特权绕过)");
    check(access(pb, X_OK) == 0, "root: pb X_OK -> 0(特权绕过)");

    /* ---------- 2. ruid=1000 而 euid=0：access 与 open 的分野 ---------- */
    check(setreuid(1000, 0) == 0, "setreuid(1000, 0) —— ruid=1000, euid 仍是 0");
    check(getuid() == 1000 && geteuid() == 0, "getuid=1000 / geteuid=0");

    /* access 按 ruid(1000) 判：1000 不是属主(2000)，走 other 位；0700 的 other 全空 */
    check(access(pb, R_OK) == -1, "ruid=1000: access(pb, R_OK) -> -1(按真实身份拒)");
    /* open 按 euid(0) 判：root 绕过，同一个文件同一个 mode 却放行 */
    int fd = open(pb, O_RDONLY);
    check(fd >= 0, "euid=0: open(pb, O_RDONLY) 成功(按有效身份放行) ← 与上一条的分野");
    if (fd >= 0) close(fd);

    check(setreuid(0, 0) == 0, "setreuid(0, 0) 回到 root(此步仍可逆)");

    /* ---------- 3. 纯非属主身份(单向门，放最后) ---------- */
    check(setreuid(1000, 1000) == 0, "setreuid(1000, 1000)");
    check(getuid() == 1000, "getuid() 现在是 1000");

    /* pa 的 other 位 = 4(r) */
    check(access(pa, R_OK) == 0, "uid=1000: pa other 有 r -> R_OK 0");
    check(access(pa, W_OK) == -1, "uid=1000: pa other 无 w -> W_OK -1");
    check(access(pa, X_OK) == -1, "uid=1000: pa other 无 x -> X_OK -1");
    check(access(pa, R_OK | W_OK) == -1, "uid=1000: pa R|W 组合 -> -1");
    check(access(pa, F_OK) == 0, "uid=1000: pa F_OK 仍为 0(与权限无关)");

    /* pb 的 other 位全空 */
    check(access(pb, R_OK) == -1, "uid=1000: pb(0700) other 全空 -> R_OK -1");
    check(access(pb, W_OK) == -1, "uid=1000: pb(0700) -> W_OK -1");

    /* 对照 open：同样按有效身份(1000)判，结论一致 */
    check(open(pa, O_RDONLY) >= 0, "对照 open(pa, O_RDONLY): other 有 r -> 成功");
    check(open(pb, O_RDONLY) == -1, "对照 open(pb, O_RDONLY): other 全空 -> 拒");

    if (fails == 0) {
        puts("\n全部通过\n");
        return 0;
    }
    printf("\n%d 项失败\n", fails);
    return 1;
}
