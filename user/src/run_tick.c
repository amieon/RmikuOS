#include "user.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: tick <cmd> [args...]\n");
        return 1;
    }

    long t0 = get_ticks();
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
        exec_with_args(argv[1], &args);
        printf("[tick] exec failed\n");
        exit(127);
    }

    int status = 0;
    waitpid(pid, &status, WUNTRACED);    
    long t1 = get_ticks();

    printf("[tick] ");
    printf(argv[1]);
    printf(" = ");
    printf("%d", (int)(t1 - t0));
    printf(" ticks, exit=");
    printf("%d", status);
    printf("\n");
    return 0;
}