#include "user.h"

/* 计时源必须是 get_time_us()，不能用 rk_get_time()。
 *
 * rk_get_time() 是**硬件裸计数器**：riscv64 的 time CSR 跑 10MHz、loongarch
 * 的 rdtime.d 跑 100MHz。这里原来直接拿它的差值当纳秒打印，于是
 *   · riscv64 上少报 100 倍 —— 跑 13 秒的 openssl verify 显示成 "131 ms"
 *   · loongarch 上少报 10 倍
 * 两个架构倍率不同，没法用常数修，只能换时钟源。get_time_us() 是内核的
 * monotonic_us()，单位就是微秒，跟架构无关。 */
int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: run_time <cmd> [args...]\n");
        return 1;
    }

    long t0 = get_time_us();
    int pid = fork();
    if (pid == 0) {
        struct exec_args args;
        /* 截断 = 子进程收到错的参数且毫无提示，宁可明确失败 */
        int n = argc - 1;
        if (n > EXEC_MAX_ARGS) {
            printf("too many arguments (max %d)\n", EXEC_MAX_ARGS);
            exit(125);
        }
        args.argc = (usize)n;
        for (int i = 0; i < EXEC_MAX_ARGS; i++) {
            if (i < n) {
                args.argv[i].ptr = argv[1 + i];
                args.argv[i].len = strlen(argv[1 + i]);
            } else {
                args.argv[i].ptr = 0;
                args.argv[i].len = 0;
            }
        }
        exec_with_args(argv[1], &args);
        printf("[time] exec failed\n");
        exit(127);
    }

    int status = 0;
    waitpid(pid, &status, WUNTRACED);
    long t1 = get_time_us();

    long dt = t1 - t0;              /* 微秒 */
    printf("[time] ");
    printf(argv[1]);
    printf(" = ");
    printf("%ld", dt);
    printf(" us(%ld ms), exit=", dt / 1000);
    /* status 是 POSIX wait status：退出码在高 8 位，被信号杀死时低 7 位是信号号 */
    printf("%d", WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status));
    printf("\n");
    return 0;
}
