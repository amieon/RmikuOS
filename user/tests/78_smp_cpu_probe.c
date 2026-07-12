#include"user.h"

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


long long my_atol(const char *str)
{
    long long sign = 1;
    long long result = 0;


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

static void wait_until(long target) {
    while (get_time() < target) {
        // busy wait
    }
}

int main(int argc, char **argv) {
    int workers = 8;
    uint64_t iters = 50000000ULL;
    long delay = 10000000L;

    if (argc >= 2) workers = my_atoi(argv[1]);
    if (argc >= 3) iters = my_strtoull(argv[2], 0, 10);
    if (argc >= 4) delay = my_atol(argv[3]);

    if (workers <= 0) workers = 1;

    long base = get_time();
    long start_at = base + delay;

    printf("[smp_probe] workers=%d iters=%llu start_at=%ld\n",
           workers, (unsigned long long)iters, start_at);

    int forked = 0;

    for (int i = 0; i < workers; i++) {
        int pid = fork();

        if (pid < 0) {
            printf("[smp_probe] fork failed at %d\n", i);
            break;
        }

        if (pid == 0) {
            wait_until(start_at);

            long h0 = hartid();
            long t0 = get_time();

            uint64_t result = worker(iters, (uint64_t)(i + 1));
            sink = result;

            long t1 = get_time();
            long h1 =  hartid();

            printf("[child] idx=%d h0=%ld h1=%ld elapsed=%ld result=%llu\n",
                   i, h0, h1, t1 - t0, (unsigned long long)(result & 0xffff));

            exit((int)((result ^ (uint64_t)i) & 0x7f));
        }

        forked++;
    }

    wait_until(start_at);

    long t0 = get_time();

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

    long t1 = get_time();

    printf("[smp_probe] done workers=%d forked=%d reaped=%d wait_fail=%d status_sum=%d elapsed=%ld\n",
           workers, forked, reaped, wait_fail, status_sum, t1 - t0);

    return 0;
}