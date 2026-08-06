#include "test.h"
#include "dirent.h"

/* 语言运行时:TCC 遍历 /codes 下所有 t*.c,逐个现场编译 + 运行 */
static void set_arg(struct exec_args *args, int i, const char *s) {
    args->argv[i].ptr = s;
    args->argv[i].len = strlen(s);
}

static int run_prog(const char *path, struct exec_args *args) {
    int pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        isize r = args ? exec_with_args(path, args) : exec(path);
        printf("exec failed: %d\n", (int)r);
        exit(127);
    }
    int code = -1;
    isize ret = waitpid(pid, &code, 0);
    if (ret != pid) return -2;
    return code;
}

/* 编译 /codes/<src>.c → /tmp/tcc_<src>,返回编译退出码 */
static int tcc_compile(const char *src) {
    struct exec_args args;
    args.argc = 4;
    set_arg(&args, 0, "/bin/tcc");
    set_arg(&args, 1, src);
    set_arg(&args, 2, "-o");
    set_arg(&args, 3, "/tmp/tcc_out");
    return run_prog("/bin/tcc", &args);
}

int main() {
    TEST_START("tcc");

    DIR *dir = opendir("/codes");
    if (!dir) {
        FAIL("无法打开 /codes");
        TEST_END();
    }

    int total = 0, passed = 0;
    struct dirent *d;
    while ((d = readdir(dir)) != 0) {
        int len = (int)strlen(d->d_name);
        if (len < 3 || d->d_name[0] != 't' || d->d_name[len - 2] != '.'
            || d->d_name[len - 1] != 'c') {
            continue;
        }

        char src[128];
        strcpy(src, "/codes/");
        strcat(src, d->d_name);
        total++;

        /* 编译 */
        int code = tcc_compile(src);
        if (code != 0) {
            printf("[PASS] %s: 编译失败(exit=%d)\n", d->d_name, code);
            continue;
        }
        /* 运行 */
        code = run_prog("/tmp/tcc_out", 0);
        if (code == 0) {
            passed++;
        } else {
            printf("[FAIL] %s: 运行失败(exit=%d)\n", d->d_name, code);
        }
    }
    closedir(dir);

    CHECK(total > 0, "发现 t*.c 测试源码");
    CHECK_EQ(passed, total, "全部 t*.c 编译+运行成功");

    TEST_END();
}
