// smp_fork_wait_bench.c
#include "user.h"


int my_atoi(const char *str)
{
    int sign = 1;
    int result = 0;


    // 跳过空格
    while (*str == ' ' ||
           *str == '\t' ||
           *str == '\n')
    {
        str++;
    }


    // 符号
    if (*str == '-')
    {
        sign = -1;
        str++;
    }
    else if (*str == '+')
    {
        str++;
    }


    // 数字
    while (*str >= '0' &&
           *str <= '9')
    {
        result = result * 10 +
                 (*str - '0');

        str++;
    }


    return result * sign;
}

int main(int argc, char **argv) {
    int rounds = 100;

    if (argc >= 2) {
        rounds = my_atoi(argv[1]);
    }

    printf("[fork_wait_bench] rounds=%d\n", rounds);

    long start = get_time();

    for (int i = 0; i < rounds; i++) {
        int pid = fork();

        if (pid < 0) {
            printf("[fork_wait_bench] fork failed at %d\n", i);
            return 1;
        }

        if (pid == 0) {
            exit(0);
        }

        int status = 0;
        wait(&status);
    }

    long end = get_time();

    printf("[fork_wait_bench] done rounds=%d elapsed_ticks=%ld\n",
           rounds, end - start);

    return 0;
}