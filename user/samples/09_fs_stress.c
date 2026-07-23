#include "user.h"

static void print_file_type_char(unsigned char type) {
    if (type == FILE_TYPE_FILE) put_char('-');
    else if (type == FILE_TYPE_DIR) put_char('d');
    else put_char('?');
}

static void print_stat(const char *path) {
    struct stat st;
    if (stat(path, &st) < 0) {
        puts("  stat失败");
        return;
    }
    put_char(' ');
    print_file_type_char(st.file_type);
    puts(" size=");
    put_int(st.size);
}

// 列出目录下所有条目（ls 风格）
static void do_ls(const char *path) {
    int fd = open(path,O_RDWR);
    if (fd < 0) {
        puts("  无法打开目录: ");
        puts(path);
        return;
    }
    puts("  目录内容:\n");
    struct dirent dirent_buf[8];
    char buf[sizeof(dirent_buf)];
    isize nread;
    int total = 0;
    while ((nread = getdents(fd, (struct dirent*)buf, sizeof(buf))) > 0) {
        struct dirent *d = (struct dirent*)buf;
        while ((char*)d < buf + nread) {
            put_char(' ');
            print_file_type_char(d->file_type);
            put_char(' ');
            write(1, d->name, d->name_len);
            put_char('\n');
            d = (struct dirent*)((char*)d + sizeof(struct dirent));
            total++;
        }
    }
    if (nread < 0) {
        puts("  getdents错误");
    }
    puts("  总计 ");
    put_int(total);
    puts(" 个条目\n");
    close(fd);
}

// 打印文件内容到 stdout (cat)
static void do_cat(const char *path) {
    int fd = open(path,O_RDWR);
    if (fd < 0) {
        puts("  无法打开文件: ");
        puts(path);
        return;
    }
    char buf[256];
    isize n;
    puts("  文件内容:\n");
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        write(1, buf, n);
    }
    if (n < 0) {
        puts("  读文件错误");
    }
    put_char('\n');
    close(fd);
}

// 打开、读全部数据然后关闭（检查 fd 泄漏）
static void do_open_read_close(const char *path) {
    int fd = open(path,O_RDWR);
    if (fd < 0) {
        puts("  open失败: ");
        puts(path);
        return;
    }
    char buf[64];
    isize n;
    usize total = 0;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        total += n;
    }
    if (n < 0) {
        puts("  read错误");
    }
    close(fd);
    puts("  读取完毕，总字节: ");
    put_int(total);
}

// ---------- 主函数 ----------
int main(int argc, char *argv[]) {
    int mypid = getpid();
    puts("\n========== FS Stress Test ==========\n");
    puts("进程 PID: ");
    put_int(mypid);
    puts("\n开始压力测试，循环 50 次...\n");

    // 预先获取当前工作目录（用于参考）
    char cwd_buf[256];
    if (getcwd(cwd_buf, sizeof(cwd_buf)) >= 0) {
        puts("当前工作目录: ");
        puts(cwd_buf);
        puts("\n");
    }

    for (int i = 1; i <= 50; i++) {
        puts("\n--- 循环 ");
        put_int(i);
        puts(" / 50 ---\n");

        // 1. ls /
        puts("[ls /]\n");
        do_ls("/");

        // 2. ls /bin
        puts("[ls /bin]\n");
        do_ls("/bin");

        // 3. cat /etc/motd (可能不存在，尝试但不退出)
        puts("[cat /etc/motd]\n");
        do_cat("/etc/motd");

        // 4. open/read/close /bin/hello (同样可能不存在)
        puts("[open/read/close /bin/hello]\n");
        do_open_read_close("/bin/hello");

        // 5. stat /bin/hello
        puts("[stat /bin/hello]\n");
        print_stat("/bin/hello");
        put_char('\n');

        // 主动让出 CPU 一下，让其他 stress 实例有机会运行（可选）
        // yield();

        // 每 10 次循环打印一个进度提示
        if (i % 10 == 0) {
            puts("\n*** 已完成 ");
            put_int(i);
            puts(" 次循环 ***\n");
        }
    }

    puts("\n========== 测试完成，进程 ");
    put_int(mypid);
    puts(" 正常退出 ==========\n");
    exit(0);
    return 0;
}