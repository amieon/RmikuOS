// 特权边界验证:降权后的进程,以及它 fork 出来的子进程,都无法重新提权到 root。
// 这证明 uid/euid 凭证在 fork 间正确继承,且非特权进程(euid!=0)不能 setuid(0)。
//
// 预期(在 init 进程 root 下运行):
//   - 初始 root(0/0/0/0)
//   - setuid(100) 成功 -> uid=euid=100(此后非特权)
//   - fork 子进程继承 100/100,子进程 setuid(0) 必须失败(-1)
//   - 父进程(已降权) setuid(0) 同样必须失败(-1)

#include "user.h"

static void show(const char *tag) {
    puts(tag);
    puts(": uid=");  printf("%d", getuid());
    puts(" euid=");  printf("%d", geteuid());
    puts("\n");
}

int main(void) {
    int fails = 0;

    puts("=== 初始应为 root ===");
    show("[init]");
    if (getuid() != 0 || geteuid() != 0) {
        puts("[FAIL] not root");
        return 1;
    }

    puts("=== 特权下降: setuid(100) -> uid=euid=100 ===");
    isize r = setuid(100);
    show("[after setuid(100)]");
    if (r != 0) { puts("[FAIL] setuid(100) should succeed as root"); fails++; }
    if (getuid() != 100 || geteuid() != 100) {
        puts("[FAIL] creds not 100/100");
        fails++;
    }

    puts("=== fork: 子进程应继承 100/100 且无法提权 ===");
    isize child = fork();
    if (child == 0) {
        show("[child]");
        if (getuid() != 100 || geteuid() != 100) {
            puts("[child FAIL] creds not inherited");
            exit(1);
        }
        isize r2 = setuid(0);   /* 非特权,必须失败 */
        puts("[child] setuid(0) ret=");
        printf("%d", r2);
        puts(" (expect -1)");
        if (r2 != -1) {
            puts("[child FAIL] non-privileged child escalated!");
            exit(1);
        }
        puts("[child OK] privilege boundary holds after fork");
        exit(0);
    } else if (child > 0) {
        int code = -1;
        waitpid(child, &code, 0);
        if (code != 0) { puts("[FAIL] child reported failure"); fails++; }
    } else {
        puts("[FAIL] fork error");
        return 1;
    }

    puts("=== 父进程(已降权)尝试提权 setuid(0) 也必须失败 ===");
    isize r3 = setuid(0);
    puts("[parent] setuid(0) ret=");
    printf("%d", r3);
    puts(" (expect -1)");
    if (r3 != -1) { puts("[FAIL] parent escalated!"); fails++; }

    puts("\n=== SUMMARY ===");
    if (fails == 0) puts("[ALL PASS]"); else puts("[SOME FAIL]");
    return fails;
}
