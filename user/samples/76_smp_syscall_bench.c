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
    int workers = 1;
    int loops = 10000;

    if (argc >= 2) {
        workers = my_atoi(argv[1]);
    }

    if (argc >= 3) {
        loops = my_atoi(argv[2]);
    }

    printf("[smp_syscall] workers=%d loops=%d\n", workers, loops);

    long start = get_time();

    for (int i = 0; i < workers; i++) {
        int pid = fork();

        if (pid < 0) {
            printf("[smp_syscall] fork failed\n");
            return 1;
        }

        if (pid == 0) {
            volatile int sum = 0;

            for (int j = 0; j < loops; j++) {
                sum += getpid();
            }

            exit(sum & 0xff);
        }
    }

    int status = 0;
    for (int i = 0; i < workers; i++) {
        wait(&status);
    }

    long end = get_time();

    printf("[smp_syscall] done workers=%d elapsed_ticks=%ld\n",
           workers, end - start);

    return 0;
}