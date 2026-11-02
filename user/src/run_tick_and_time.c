#include "user.h"

/* 这里同时给两种时间，单位必须区分清楚：
 *   [tick] 来自 get_ticks()，是内核调度节拍，单位就是 tick；
 *   [time] 必须来自 get_time_us()（内核 monotonic_us，单位微秒）。
 *
 * 原来 [time] 用的是 rk_get_time()，那是**硬件裸计数器**（riscv64 10MHz、
 * loongarch 100MHz），却被当纳秒打印 —— riscv64 上少报 100 倍。两个架构
 * 倍率不同，修不了，只能换 get_time_us()。 */
int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: run_tick_and_time <cmd> [args...]\n");
        return 1;
    }

    long tick0 = get_ticks();
    long time0 = get_time_us();
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
        printf("[tick] exec failed\n");
        exit(127);
    }

    int status = 0;
    waitpid(pid, &status, WUNTRACED);
    long tick1 = get_ticks();
    long time1 = get_time_us();

    long dt = time1 - time0;        /* 微秒 */
    printf("[tick] ");
    printf(argv[1]);
    printf(" = ");
    printf("%ld", tick1 - tick0);
    printf(" ticks\n");
    printf("[time] ");
    printf(argv[1]);
    printf(" = ");
    printf("%ld us (%ld ms)\n", dt, dt / 1000);
    printf("exit=");
    /* status 是 POSIX wait status：退出码在高 8 位，被信号杀死时低 7 位是信号号 */
    printf("%d", WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status));
    printf("\n");
    return 0;
}
