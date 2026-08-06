#include "test.h"
#include "dirent.h"

/* 语言运行时:Scheme 遍历 /codes 下所有 *.scm 脚本逐个运行 */
static void set_arg(struct exec_args *args, int i, const char *s) {
    args->argv[i].ptr = s;
    args->argv[i].len = strlen(s);
}

static int run_scheme(const char *script) {
    struct exec_args args;
    args.argc = 2;
    set_arg(&args, 0, "/programs/scheme");
    set_arg(&args, 1, script);

    int pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        isize r = exec_with_args("/programs/scheme", &args);
        printf("exec failed: %d\n", (int)r);
        exit(127);
    }
    int code = -1;
    isize ret = waitpid(pid, &code, 0);
    if (ret != pid) return -2;
    return code;
}

int main() {
    TEST_START("lang_scheme");

    DIR *dir = opendir("/codes");
    if (!dir) {
        FAIL("无法打开 /codes");
        TEST_END();
    }

    int total = 0, passed = 0;
    struct dirent *d;
    while ((d = readdir(dir)) != 0) {
        int len = (int)strlen(d->d_name);
        if (len < 5 || strcmp(d->d_name + len - 4, ".scm") != 0) {
            continue;
        }

        char script[128];
        strcpy(script, "/codes/");
        strcat(script, d->d_name);
        total++;

        int code = run_scheme(script);
        if (code == 0) {
            passed++;
        } else {
            printf("[FAIL] %s: 退出码 %d\n", d->d_name, code);
        }
    }
    closedir(dir);

    CHECK(total > 0, "发现 *.scm 测试脚本");
    CHECK_EQ(passed, total, "全部 scheme 脚本运行成功");

    TEST_END();
}
