// 好玩的多进程蒙特卡洛 π 估计程序
#include "user.h"

// ---------- 工具函数 ----------
// 简易 atoi，从字符串开头解析整数
static int atoi(const char *s) {
    int val = 0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    return val;
}

// 读取一行（最多 maxlen-1 个字符），返回长度，字符串以 '\0' 结尾
static int readline(char *buf, int maxlen) {
    int i = 0;

    while (i < maxlen - 1) {
        char ch = 0;

        isize n = read(0, &ch, 1);
        if (n <= 0) {
            continue;
        }

        if (ch == '\r') {
            ch = '\n';
        }

        if (ch == '\n') {
            put_char('\n');
            break;
        }

        if (ch == 8 || ch == 127) {
            if (i > 0) {
                i--;
                write(1, "\b \b", 3);
            }
            continue;
        }

        buf[i++] = ch;
        put_char(ch);   // 回显输入
    }

    buf[i] = '\0';
    return i;
}

// 简易的 xorshift 随机数生成器（每个子进程独立种子）
// 生成 [0, 1) 之间的双精度浮点数（用定点整数模拟，返回 0..RAND_MAX）
#define RAND_MAX 0x7FFFFFFF
static unsigned int rand_u32(unsigned int *state) {
    unsigned int x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static int rand_int(unsigned int *state) {
    return (int)(rand_u32(state) & RAND_MAX);
}

// 蒙特卡洛投点：在 [0,1) 正方形内随机投点，统计落在四分之一圆内的数量
static int monte_carlo_points(int num_points, unsigned int *seed) {
    int inside = 0;

    for (int i = 0; i < num_points; i++) {
        int x = rand_int(seed);
        int y = rand_int(seed);

        long long dx = x;
        long long dy = y;
        // x^2 + y^2 <= R^2，其中 R = RAND_MAX
        // 为避免浮点，比较平方和
        if (dx * dx + dy * dy <= (long long)RAND_MAX * RAND_MAX) {
            inside++;
        }
    }

    return inside;
}
// ---------- 主程序 ----------
int main() {
    int num_children = 2;
    int points_per_child = 1000;

    // 交互式读入参数（演示 read 用法）
    char buf[64];
    puts("=== 多进程蒙特卡洛 π 估计程序 ===\n");
    puts("请输入子进程数量 (默认为2): ");
    if (readline(buf, sizeof(buf)) > 0 && buf[0] != '\0') {
        num_children = atoi(buf);
        if (num_children <= 0) num_children = 1;
        if (num_children > 32) num_children = 32;  // 防止过多
    }
    puts("请输入每个子进程的投点数 (默认为10000): ");
    if (readline(buf, sizeof(buf)) > 0 && buf[0] != '\0') {
        points_per_child = atoi(buf);
        if (points_per_child <= 0) points_per_child = 1000;
        if (points_per_child > 100000) points_per_child = 100000;
    }

    puts("\n开始创建子进程...\n");

    int child_pids[32];      // 存储子进程 pid
    int child_results[32];   // 存储每个子进程的圆内点数
    int child_count = 0;

    for (int i = 0; i < num_children; i++) {
        isize pid = fork();
        if (pid < 0) {
            puts("fork 失败！\n");
            exit(1);
        } else if (pid == 0) {
            // 子进程
            int mypid = getpid();
            puts("[子进程 ");
            put_int(mypid);
            puts("] 启动，投点次数: ");
            put_int(points_per_child);
            puts("\n");

            // 用自身 pid 和时间（简易）做种子，保证不同子进程随机序列不同
            unsigned int seed = (unsigned int)(mypid * 1664525 + 1013904223);

            int inside = monte_carlo_points(points_per_child, &seed);
            puts("[子进程 ");
            put_int(mypid);
            puts("] 圆内点数: ");
            put_int(inside);
            puts("\n");

            exit(inside);   // 通过退出码返回结果
        } else {
            // 父进程记录 pid
            child_pids[child_count++] = (int)pid;
        }
    }

    // 父进程等待所有子进程结束，收集结果
    puts("\n父进程等待所有子进程完成...\n");
    int total_inside = 0;
    int total_points = 0;
    for (int i = 0; i < child_count; i++) {
        int exit_code;
        isize ret = waitpid(child_pids[i], &exit_code, 0);
        if (ret < 0) {
            puts("waitpid 失败\n");
        } else {
            total_inside += exit_code;
            total_points += points_per_child;
            puts("子进程 ");
            put_int(child_pids[i]);
            puts(" 结束，返回圆内点数: ");
            put_int(exit_code);
            puts("\n");
        }
    }

    // 计算结果
    if (total_points == 0) {
        puts("没有有效投点数据！\n");
        exit(0);
    }

    // 估算 π = 4 * (圆内点数 / 总点数)
    // 使用定点整数计算，保留小数点后 6 位
    long long pi_numerator = 4LL * total_inside * 1000000LL;
    long long pi_approx = pi_numerator / total_points;
    long long pi_integer = pi_approx / 1000000;
    long long pi_fraction = pi_approx % 1000000;

    puts("\n========== 最终结果 ==========\n");
    puts("总投点数: ");
    put_int(total_points);
    puts("\n总圆内点数: ");
    put_int(total_inside);
    puts("\n估算的 π ≈ ");
    put_int(pi_integer);
    put_char('.');
    // 补零到 6 位小数
    int frac_digits[6];
    for (int i = 5; i >= 0; i--) {
        frac_digits[i] = pi_fraction % 10;
        pi_fraction /= 10;
    }
    for (int i = 0; i < 6; i++) {
        put_char('0' + frac_digits[i]);
    }
    puts("\n真实 π ≈ 3.1415926535");
    puts("\n===============================\n");

    // 可选：父进程自己再做一个简单的性能演示
    puts("按回车键退出程序...\n");
    readline(buf, sizeof(buf));

    exit(0);
    return 0;
}