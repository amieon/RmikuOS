#include "user.h"
#include "dirent.h"

/* ============================================================================
 * run_all —— RmikuOS 测试批量执行器(进 /bin/run_all)
 *
 * 遍历 /tests/ 下所有测试程序(*_test 或 sqlite_test),逐个 fork+exec+waitpid,
 * 按退出码判定 PASS/FAIL(0=全过,1=有失败断言,127=exec 失败),最后汇总。
 *
 * 输出约定(机器可解析):
 *   [RUNALL] 共发现 N 个测试
 *   [RUNALL] <name> ... PASS
 *   [RUNALL] <name> ... FAIL (exit=X)
 *   [RUNALL] 汇总: X passed, Y failed
 * 退出码:0=全部通过, 1=有失败。
 * ==========================================================================*/

#define MAX_TESTS 128
#define NAME_MAX_LEN 64

static int is_test_name(const char *name) {
    int len = (int)strlen(name);
    if (len >= 6 && strcmp(name + len - 6, "_test") == 0) return 1;
    if (strcmp(name, "sqlite_test") == 0) return 1;
    return 0;
}

int main(void) {
    char names[MAX_TESTS][NAME_MAX_LEN];
    int count = 0;

    DIR *dir = opendir("/tests");
    if (!dir) {
        puts("run_all: 无法打开 /tests\n");
        return 1;
    }

    struct dirent *d;
    while ((d = readdir(dir)) != 0) {
        if (count >= MAX_TESTS) break;
        if (is_test_name(d->d_name)) {
            strncpy(names[count], d->d_name, NAME_MAX_LEN - 1);
            names[count][NAME_MAX_LEN - 1] = 0;
            count++;
        }
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
