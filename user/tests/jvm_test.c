#include "test.h"

/* 语言运行时:JVM 运行 /jvm/hello_java/hello_java.class */
static void set_arg(struct exec_args *args, int i, const char *s) {
    args->argv[i].ptr = s;
    args->argv[i].len = strlen(s);
}

int main() {
    TEST_START("jvm");

    struct exec_args args;
    args.argc = 2;
    set_arg(&args, 0, "/programs/jvm");
    set_arg(&args, 1, "/jvm/hello_java/hello_java.class");

    int pid = fork();
    if (pid < 0) {
        FAIL("fork 失败");
        TEST_END();
    }
    if (pid == 0) {
        isize r = exec_with_args("/programs/jvm", &args);
        printf("exec failed: %d\n", (int)r);
        exit(127);
    }
    int code = -1;
    isize ret = waitpid(pid, &code, 0);
    CHECK(ret == pid, "waitpid 回收 jvm 子进程");
    CHECK_EQ(code, 0, "jvm 运行 hello_java.class 成功(exit 0)");

    TEST_END();
}
