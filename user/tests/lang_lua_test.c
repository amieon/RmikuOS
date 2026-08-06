#include "test.h"
#include "dirent.h"

/* 语言运行时:Lua 遍历 /scripts/lua 下所有 0*.lua 脚本逐个运行 */
static void set_arg(struct exec_args *args, int i, const char *s) {
    args->argv[i].ptr = s;
    args->argv[i].len = strlen(s);
}

static int run_lua(const char *script) {
    struct exec_args args;
    args.argc = 2;
    set_arg(&args, 0, "/programs/lua");
    set_arg(&args, 1, script);

    int pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        isize r = exec_with_args("/programs/lua", &args);
        printf("exec failed: %d\n", (int)r);
        exit(127);
    }
    int code = -1;
    isize ret = waitpid(pid, &code, 0);
    if (ret != pid) return -2;
    return code;
}

int main() {
    TEST_START("lang_lua");

    DIR *dir = opendir("/scripts/lua");
    if (!dir) {
        FAIL("无法打开 /scripts/lua");
        TEST_END();
    }

    int total = 0, passed = 0;
    struct dirent *d;
    while ((d = readdir(dir)) != 0) {
        int len = (int)strlen(d->d_name);
        /* 匹配 0*.lua:以 0 开头且以 .lua 结尾(后缀起点在 len-4 处) */
        if (len < 5 || d->d_name[0] != '0'
            || strcmp(d->d_name + len - 4, ".lua") != 0) {
            continue;
        }

        char script[128];
        strcpy(script, "/scripts/lua/");
        strcat(script, d->d_name);
        total++;

        int code = run_lua(script);
        if (code == 0) {
            passed++;
        } else {
            printf("[FAIL] %s: 退出码 %d\n", d->d_name, code);
        }
    }
    closedir(dir);

    CHECK(total > 0, "发现 0*.lua 测试脚本");
    CHECK_EQ(passed, total, "全部 lua 脚本运行成功");

    TEST_END();
}
