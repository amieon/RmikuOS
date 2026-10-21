#include "user.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: tick <cmd> [args...]\n");
        return 1;
    }

    long tick0 = get_ticks();
    long time0 = rk_get_time();
    int pid = fork();
    if (pid == 0) {
        struct exec_args args;
        int n = argc - 1;
        if (n > EXEC_MAX_ARGS) {
            n = EXEC_MAX_ARGS;
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
        tick0 = get_ticks();
        time0 = rk_get_time();
        exec_with_args(argv[1], &args);
        printf("[tick] exec failed\n");
        exit(127);
    }

    int status = 0;
    waitpid(pid, &status, WUNTRACED);    
    long tick1 = get_ticks();
    long time1 = rk_get_time();

    printf("[tick] ");
    printf(argv[1]);
    printf(" = ");
    printf("%d", (long)(tick1 - tick0));
    printf(" ticks\n");
    printf("[time] ");
    printf(argv[1]);
    printf(" = ");
    printf("%d ns (%d ms)\n", (long)(time1 - time0), (long)((time1 - time0) / 1000000));
    printf("exit=");
    /* status 是 POSIX wait status：退出码在高 8 位，被信号杀死时低 7 位是信号号 */
    printf("%d", WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status));
    printf("\n");
    return 0;
}