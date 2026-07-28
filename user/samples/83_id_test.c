// 进程凭证 / 权限系统验证程序。
// 验证:get* 查询、fork 继承、特权(root euid==0)下的 set* 行为、降权后受限。
//
// 预期(在 init 进程 root 下运行):
//   - 初始 uid/euid/gid/egid 全为 0
//   - fork 的子进程继承 0/0/0/0
//   - 特权下 seteuid(100) 成功 -> 0/100/0/0；seteuid(0) 成功 -> 0/0/0/0
//   - 特权下 setuid(100) 成功 -> uid=euid=100（此后非特权）
//   - 非特权下 setuid(0) 失败(-1)

#include "user.h"

static void show_creds(const char *tag) {
    puts(tag);
    puts(": uid=");  printf("%d", getuid());
    puts(" euid=");  printf("%d", geteuid());
    puts(" gid=");   printf("%d", getgid());
    puts(" egid=");  printf("%d", getegid());
    puts("");
}

static int expect(const char *tag, isize got, isize want) {
    puts(tag);
    puts(" => got=");  printf("%d", got);
    puts(" want=");    printf("%d", want);
    if (got == want) {
        puts("  [OK]");
        return 0;
    }
    puts("  [FAIL]");
    return 1;
}

int main(void) {
    int fails = 0;

    puts("=== test: 初始凭证应为 root(0/0/0/0) ===");
    show_creds("[init]");
    fails += expect("[init] uid",  getuid(),  0);
    fails += expect("[init] euid", geteuid(), 0);
    fails += expect("[init] gid",  getgid(),  0);
    fails += expect("[init] egid", getegid(), 0);

    puts("=== test: fork 继承凭证 ===");
    isize child = fork();
    if (child == 0) {
        show_creds("[child]");
        fails += expect("[child] uid",  getuid(),  0);
        fails += expect("[child] euid", geteuid(), 0);

        // 子进程以 root 身份把 euid 降到 100，再升回 0。
        isize r1 = seteuid(100);
        show_creds("[child after seteuid(100)]");
        fails += expect("[child] seteuid(100) ret", r1, 0);
        fails += expect("[child] euid now", geteuid(), 100);

        isize r2 = seteuid(0);
        fails += expect("[child] seteuid(0) ret", r2, 0);
        fails += expect("[child] euid back", geteuid(), 0);

        puts("[child] exit 0");
        exit(0);
    } else if (child > 0) {
        int code = -1;
        waitpid(child, &code, 0);
    }

    puts("=== test: 特权 setuid 降权,随后非特权 setuid 提权应失败 ===");
    isize s1 = setuid(100);           // 特权:uid=euid=100
    show_creds("[parent after setuid(100)]");
    fails += expect("[parent] setuid(100) ret", s1, 0);
    fails += expect("[parent] uid",  getuid(),  100);
    fails += expect("[parent] euid", geteuid(), 100);

    isize s2 = setuid(0);             // 非特权(euid=100):不允许 -> -1
    show_creds("[parent after setuid(0) attempt]");
    fails += expect("[parent] setuid(0) ret (should fail)", s2, -1);

    // setreuid 部分修改:仅改 euid 回 0(非特权下 euid 只能设成当前 uid/euid)。
    isize s3 = setreuid((usize)-1, 100);
    fails += expect("[parent] setreuid(-1,100) ret", s3, 0);
    fails += expect("[parent] euid still", geteuid(), 100);

    puts("=== test: 非特权 setuid 到无关 id 应失败 ===");
    isize s4 = setuid(200);            // 200 != 100(uid) 且 != 100(euid) -> -1
    fails += expect("[parent] setuid(200) ret (should fail)", s4, -1);

    puts("\n=== SUMMARY ===");
    if (fails == 0) {
        puts("[ALL PASS]");
    } else {
        puts("[SOME FAIL]");
    }
    return fails;
}
