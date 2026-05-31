// 程序：多进程斐波那契大赛
// 每个子进程计算 Fibonacci(n)，父进程等待所有子进程完成并宣布冠军
// 演示 fork, waitpid, exit, getpid, sleep, put_int, puts, read

#include "user.h"

// 工具函数：读取一行（前面已经提供，这里简化）
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

// 计算斐波那契数列的第 n 项（迭代法，避免递归爆栈）
static unsigned long fib(unsigned int n) {
    if (n == 0) return 0;
    if (n == 1) return 1;
    unsigned long a = 0, b = 1;
    for (unsigned int i = 2; i <= n; i++) {
        unsigned long c = a + b;
        a = b;
        b = c;
    }
    return b;
}

// 让子进程做一些“额外工作”来模拟不同的负载：如果 work_type 非0，则 sleep 一下
static void do_extra_work(int work_type, int pid) {
    if (work_type == 0) return;
    puts("[子进程 ");
    put_int(pid);
    puts("] 开始额外休息，模拟 I/O 阻塞...\n");
    sleep(work_type);  // sleep ticks
    puts("[子进程 ");
    put_int(pid);
    puts("] 休息结束，继续计算。\n");
}

int main(int argc, char *argv[]) {
    puts("\n========== 多进程斐波那契大赛 ==========\n");
    puts("请输入斐波那契项数 n (推荐 30~45，太大可能溢出或太慢): ");

    char buf[32];
    readline(buf, sizeof(buf));
    unsigned int n = 0;
    for (char *p = buf; *p >= '0' && *p <= '9'; p++) {
        n = n * 10 + (*p - '0');
    }
    if (n == 0) n = 35;  // 默认值
    puts("计算 Fibonacci(");
    put_int(n);
    puts(")\n");

    puts("请输入子进程数量 (推荐 5~10): ");
    readline(buf, sizeof(buf));
    int num_procs = 0;
    for (char *p = buf; *p >= '0' && *p <= '9'; p++) {
        num_procs = num_procs * 10 + (*p - '0');
    }
    if (num_procs <= 0) num_procs = 5;
    if (num_procs > 20) num_procs = 20;  // 避免过度fork

    puts("\n开始创建 ");
    put_int(num_procs);
    puts(" 个子进程，每个都将独立计算 Fib(...)\n");

    int child_pids[20];
    int child_count = 0;

    for (int i = 0; i < num_procs; i++) {
        isize pid = fork();
        if (pid < 0) {
            puts("fork 失败！可能内核进程表满了？\n");
            break;
        } else if (pid == 0) {
            // 子进程
            int mypid = getpid();

            // 让部分子进程（i 为奇数）做额外休息，模拟不同行为
            do_extra_work((i % 3) * 2, mypid);

            puts("[子进程 ");
            put_int(mypid);
            puts("] 开始计算 Fibonacci(");
            put_int(n);
            puts(")...\n");

            unsigned long result = fib(n);

            puts("[子进程 ");
            put_int(mypid);
            puts("] 计算完成！结果是: ");
            put_int(result);   // 注：long 可能超过 int 范围，但 put_int 支持 long
            puts("\n");

            // 将结果通过退出码返回（但退出码只有低8位会被内核保留，这里我们只展示用法；
            // 若要传递完整结果，可以通过共享内存或文件，不过为了简单，让子进程直接打印）
            // 我们依然返回结果的低8位作为退出状态，方便父进程接收并检查是否一致。
            exit(result & 0xFF);
        } else {
            child_pids[child_count++] = (int)pid;
        }
    }

    // 父进程等待所有子进程结束，并记录他们的退出码
    puts("\n=== 父进程等待子进程结束并回收 ===\n");
    int total_exit_sum = 0;
    int finished = 0;
    for (int i = 0; i < child_count; i++) {
        int exit_code;
        isize ret = waitpid(child_pids[i], &exit_code);
        if (ret < 0) {
            puts("waitpid 失败！子进程 ");
            put_int(child_pids[i]);
            puts(" 可能已经异常？\n");
        } else {
            puts("子进程 ");
            put_int(child_pids[i]);
            puts(" 已回收，退出码: ");
            put_int(exit_code);
            puts("\n");
            total_exit_sum += exit_code;
            finished++;
        }
    }

    puts("\n========== 大赛总结 ==========\n");
    puts("成功回收 ");
    put_int(finished);
    puts(" 个子进程（共 ");
    put_int(child_count);
    puts(" 个）。\n");
    puts("所有子进程退出码之和（低8位）: ");
    put_int(total_exit_sum);
    puts("\n注意：真正的斐波那契结果已经在各子进程输出中打印。\n");
    puts("如果父进程没有卡死且正确回收了所有子进程，说明 fork/waitpid/exit 工作正常。\n");

    // 额外测试：父进程自己再计算一次，验证正确性
    puts("\n父进程自行验证计算 Fibonacci(");
    put_int(n);
    puts(") = ");
    unsigned long parent_result = fib(n);
    put_int(parent_result);
    puts("\n");

    puts("\n按回车键退出程序...\n");
    readline(buf, sizeof(buf));
    exit(0);
    return 0;
}