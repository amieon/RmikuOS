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



unsigned long long my_strtoull(
        const char *str,
        char **endptr,
        int base)
{
    unsigned long long result = 0;


    // 跳过空格
    while (*str == ' ' ||
           *str == '\t' ||
           *str == '\n')
    {
        str++;
    }


    // 自动判断进制
    if (base == 0)
    {
        if (str[0] == '0')
        {
            if (str[1] == 'x' ||
                str[1] == 'X')
            {
                base = 16;
                str += 2;
            }
            else
            {
                base = 8;
            }
        }
        else
        {
            base = 10;
        }
    }


    while (1)
    {
        char c = *str;

        int value;


        if (c >= '0' && c <= '9')
        {
            value = c - '0';
        }
        else if (c >= 'a' && c <= 'f')
        {
            value = c - 'a' + 10;
        }
        else if (c >= 'A' && c <= 'F')
        {
            value = c - 'A' + 10;
        }
        else
        {
            break;
        }


        // 超过进制范围
        if (value >= base)
        {
            break;
        }


        result = result * base + value;

        str++;
    }


    if (endptr)
    {
        *endptr = (char *)str;
    }


    return result;
}
typedef unsigned long long uint64_t ;
static volatile uint64_t sink = 0;
static uint64_t worker(uint64_t iters, uint64_t seed) {
    uint64_t x = seed ^ 0x9e3779b97f4a7c15ULL;

    for (uint64_t i = 0; i < iters; i++) {
        x ^= x << 7;
        x ^= x >> 9;
        x *= 0x2545f4914f6cdd1dULL;
        x += i ^ seed;
    }

    return x;
}

int main(int argc, char **argv) {
    int workers = 1;
    uint64_t iters = 50000000ULL;

    if (argc >= 2) workers = my_atoi(argv[1]);
    if (argc >= 3) iters = my_strtoull(argv[2], 0, 10);
    if (workers <= 0) workers = 1;

    printf("[smp_cpu_quiet] workers=%d iters_per_worker=%llu\n",
           workers, (unsigned long long)iters);

    long start = get_time();

    int forked = 0;

    for (int i = 0; i < workers; i++) {
        int pid = fork();

        if (pid < 0) {
            break;
        }

        if (pid == 0) {
            uint64_t result = worker(iters, (uint64_t)(i + 1));
            sink = result;
            exit((int)((result ^ (uint64_t)i) & 0x7f));
        }

        forked++;
    }

    int reaped = 0;
    int wait_fail = 0;
    int status_sum = 0;

    while (reaped < forked) {
        int status = 0;
        int pid = wait(&status);

        if (pid < 0) {
            wait_fail++;
            break;
        }

        reaped++;
        status_sum += status;
    }

    long end = get_time();

    printf("[smp_cpu_quiet] done workers=%d forked=%d reaped=%d wait_fail=%d status_sum=%d elapsed_ticks=%ld\n",
           workers, forked, reaped, wait_fail, status_sum, end - start);

    return 0;
}