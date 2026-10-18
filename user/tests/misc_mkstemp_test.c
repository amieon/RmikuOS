/* mkstemp / mkdtemp 测试
 *
 * 不只是"能建文件"，而是验证它挡住的那类攻击：
 *   1. 模板末尾 6 个 X 必须被替换，且返回的是**已打开的 fd**（不是名字）
 *   2. 权限固定 0600 / 0700 —— **不受 umask 影响**（umask(0) 也必须 0600）
 *   3. 攻击原语：目标名已被 symlink 占位时，O_CREAT|O_EXCL 必须失败，
 *      绝不能顺着链接改到别处（这就是 CWE-377 的核心）
 *   4. 非法模板要明确失败，不能瞎建
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
    const char *d = "/tmp/mkst";
    char t1[64];
    char t2[64];
    char td[64];
    struct stat st;

    puts("=== mkstemp / mkdtemp 测试 ===\n");

    remove_recursive(d);
    check(mkdir(d, 0755) == 0, "mkdir /tmp/mkst");

    /* ---------- 1. 基本行为 ---------- */
    strcpy(t1, "/tmp/mkst/aXXXXXX");
    int fd = mkstemp(t1);
    check(fd >= 0, "mkstemp 返回有效 fd");
    check(write(fd, "secret", 6) == 6, "通过返回的 fd 直接写(不需要再按名字打开)");
    check(stat(t1, &st) == 0, "模板被就地改成实际文件名，该文件存在");
    check(strlen(t1) == 17, "名字长度正确(/tmp/mkst/a + 6)");

    int no_x = 1;
    for (usize i = 0; i < strlen(t1); ++i) {
        if (t1[i] == 'X') no_x = 0;
    }
    check(no_x, "模板里的 6 个 X 全部被替换");

    /* 读回来（lseek 到 0 再读，验证 fd 是可读写的） */
    char buf[16];
    check(lseek(fd, 0, SEEK_SET) == 0, "lseek 回 0");
    int n = (int)read(fd, buf, sizeof(buf));
    check(n == 6 && buf[0] == 's' && buf[5] == 't', "读回写入的内容");
    close(fd);

    /* ---------- 2. 两个调用不能撞名 ---------- */
    strcpy(t2, "/tmp/mkst/aXXXXXX");
    int fd2 = mkstemp(t2);
    check(fd2 >= 0, "第二次 mkstemp 成功");
    check(strcmp(t1, t2) != 0, "两次生成的临时名不同");
    close(fd2);

    /* ---------- 3. 权限固定，不受 umask 影响 ---------- */
    mode_t old = umask(0);          /* 把掩码放到最松 */
    strcpy(td, "/tmp/mkst/bXXXXXX");
    int fd3 = mkstemp(td);
    check(fd3 >= 0, "umask=0 下 mkstemp 仍成功");
    if (fd3 >= 0) close(fd3);
    check(stat(td, &st) == 0 && (st.st_mode & 0777) == 0600,
          "umask=0 时临时文件仍是 0600(权限不由 umask 决定)");
    umask(old);

    /* mkdtemp：目录应当是 0700，而不是 0755 */
    strcpy(td, "/tmp/mkst/cXXXXXX");
    check(mkdtemp(td) != (char *)0, "mkdtemp 返回模板指针");
    check(stat(td, &st) == 0 && S_ISDIR(st.st_mode), "mkdtemp 建出的是目录");
    check((st.st_mode & 0777) == 0700, "临时目录权限 0700");

    /* ---------- 4. 攻击原语：symlink 占位 ---------- */
    /* 攻击者把 victim 名做成指向"敏感文件"的符号链接；
     * 老式程序 fopen(name,"w") 会顺着链接把 guard.txt 覆盖掉。
     * mkstemp 依赖的 O_CREAT|O_EXCL 必须让这个 open 直接失败。 */
    const char *guard = "/tmp/mkst/guard.txt";
    const char *bait = "/tmp/mkst/bait";
    int g = open(guard, O_CREAT | O_WRONLY, 0600);
    check(g >= 0, "创建要被保护的 guard.txt");
    if (g >= 0) {
        write(g, "ORIGINAL", 8);
        close(g);
    }
    check(symlink(guard, bait) == 0, "攻击者把 bait 做成指向 guard.txt 的 symlink");

    int evil = open(bait, O_CREAT | O_EXCL | O_WRONLY, 0600);
    check(evil == -1, "目标被 symlink 占位时 O_CREAT|O_EXCL 必须失败");
    if (evil >= 0) close(evil);

    /* 确认 guard.txt 内容没被动过 —— 这才是攻击成不成的判据 */
    int r = open(guard, O_RDONLY);
    check(r >= 0, "重新打开 guard.txt");
    if (r >= 0) {
        char gb[16];
        int gn = (int)read(r, gb, sizeof(gb));
        check(gn == 8 && gb[0] == 'O' && gb[7] == 'L', "guard.txt 内容未被篡改");
        close(r);
    }

    /* ---------- 5. 非法模板 ---------- */
    char bad1[] = "/tmp/mkst/noxx";
    check(mkstemp(bad1) == -1, "没有 6 个 X 结尾的模板必须失败");
    char bad2[] = "XXXXX";
    check(mkstemp(bad2) == -1, "模板不足 6 个字符必须失败");
    char bad3[] = "/tmp/XXXXXX/foo";
    check(mkstemp(bad3) == -1, "X 不在结尾必须失败");

    /* ---------- 6. 清理 ---------- */
    unlink(bait);
    remove_recursive(d);

    puts("\n");
    if (fails) {
        printf("%d 项失败\n", fails);
        return 1;
    }
    puts("all passed\n");
    return 0;
}
