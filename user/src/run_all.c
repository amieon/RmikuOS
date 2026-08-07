#include "user.h"
#include "dirent.h"

/*
 * run_all —— RmikuOS 测试批量执行器(进 /bin/run_all)
 *
 * 直接运行 /tests/ 下所有程序(该目录语义上就是"测试专区",mkfs 只放测试
 * 可执行文件,全跑永不漏测;若混入非程序文件会 exec 失败 → 红,反而提醒)。
 * 逐个 fork+exec+waitpid,按退出码判定 PASS/FAIL(0=全过,1=有失败断言,
 * 127=exec 失败),最后汇总。
 *
 * 输出约定(机器可解析):
 *   [RUNALL] 共发现 N 个测试
 *   [RUNALL] <name> ... PASS
 *   [RUNALL] <name> ... FAIL (exit=X)
 *   [RUNALL] 汇总: X passed, Y failed
 * 退出码:0=全部通过, 1=有失败。
 */

#define MAX_TESTS 128
#define NAME_MAX_LEN 64

int main(int argc, char *argv[]) {
    char names[MAX_TESTS][NAME_MAX_LEN];
    int count = 0;

    DIR *dir = opendir("/tests");
    if (!dir) {
        puts("run_all: 无法打开 /tests\n");
        return 1;
    }

    /* -x <name>:忽略指定测试(可多次)。例:run_all -x tcc_test */
    const char *skip[16];
    int nskip = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-x") == 0 && i + 1 < argc && nskip < 16) {
            skip[nskip++] = argv[++i];
        }
    }

    struct dirent *d;
    while ((d = readdir(dir)) != 0) {
        if (count >= MAX_TESTS) break;
        /* 跳过 . 和 .. */
        if (strcmp(d->d_name, ".") == 0 || strcmp(d->d_name, "..") == 0) continue;
        int skipped = 0;
        for (int j = 0; j < nskip; j++) {
            if (strcmp(d->d_name, skip[j]) == 0) { skipped = 1; break; }
        }
        if (skipped) continue;
        strncpy(names[count], d->d_name, NAME_MAX_LEN - 1);
        names[count][NAME_MAX_LEN - 1] = 0;
        count++;
    }
    closedir(dir);

    printf("[RUNALL] 共发现 %d 个测试\n", count);

    int passed = 0, failed = 0;
    for (int i = 0; i < count; i++) {
        char path[128];
        strcpy(path, "/tests/");
        strcat(path, names[i]);

        printf("\n===== %s =====\n", names[i]);

        int pid = fork();
        if (pid < 0) {
            printf("[RUNALL] %s ... FAIL (fork 失败)\n", names[i]);
            failed++;
            continue;
        }
        if (pid == 0) {
            isize r = exec(path);
            /* exec 失败才会到这 */
            printf("exec failed: %d\n", (int)r);
            exit(127);
        }

        int code = -1;
        isize ret = waitpid(pid, &code, 0);
        if (ret != pid) {
            printf("[RUNALL] %s ... FAIL (waitpid 失败)\n", names[i]);
            failed++;
            continue;
        }

        if (code == 0) {
            printf("[RUNALL] %s ... PASS\n", names[i]);
            passed++;
        } else {
            printf("[RUNALL] %s ... FAIL (exit=%d)\n", names[i], code);
            failed++;
        }
    }

    printf("\n[RUNALL] 汇总: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
