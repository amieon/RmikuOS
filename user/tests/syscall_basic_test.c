#include "test.h"

/* 系统调用基础:pid / argv / 环境变量 */
int main(int argc, char *argv[]) {
    TEST_START("syscall_basic");

    isize pid = getpid();
    CHECK(pid > 0, "getpid 返回正数");
    CHECK(getppid() > 0, "getppid 返回正数");

    CHECK(argc >= 1, "argc >= 1");
    CHECK(argv[0] != 0 && argv[0][0] != 0, "argv[0] 非空");

    isize r = setenv_s("TEST_KEY", "hello_env", 1);
    CHECK(r >= 0, "setenv 成功");
    char *v = getenv("TEST_KEY");
    CHECK(v != 0, "getenv 能查到刚设的变量");
    CHECK_STREQ(v, "hello_env", "环境变量值一致");

    TEST_END();
}
