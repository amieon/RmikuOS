/* 宿主机语法自检：复刻 shell.c 在 POSIX 路径的 include 顺序。
 * 只做 -fsyntax-only, 不链接。运行: gcc -Iuser/include -fsyntax-only this.c
 * 本机没有 syscall3/memset 等符号的定义, 但语法检查不需要链接。 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <ctype.h>
#include <stdarg.h>
#include <signal.h>
#include <pwd.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <errno.h>
#include <limits.h>
#include <memory.h>
#include <fcntl.h>
#include <time.h>

int main(void) {
    struct stat st;
    mode_t m;
    uid_t u;
    time_t t;
    if (stat("x", &st) != 0) return 1;
    if (lstat("x", &st) != 0) return 1;
    if (fstat(0, &st) != 0) return 1;
    m = st.st_mode; u = st.st_uid; t = st.st_mtime;
    if (S_ISREG(m) || S_ISDIR(m) || S_ISLNK(m) || S_ISCHR(m) || S_ISFIFO(m)) {}
    if (mkdir("x", 0777) != 0) {}
    if (chmod("x", 0755) != 0) {}
    if (chown("x", 0, 0) != 0) {}
    if (unlink("x") != 0) {}
    if (rmdir("x") != 0) {}
    if (rename("a", "b") != 0) {}
    if (chdir("x") != 0) {}
    char buf[512];
    if (getcwd(buf, sizeof buf) == 0) return 1;
    if (lseek(0, 0, SEEK_SET) < 0) return 1;
    if (ftruncate(0, 0) != 0) {}
    if (fsync(0) != 0) {}
    if (truncate("x", 0) != 0) {}
    if (isatty(0) == 0) {}
    if (access("x", 0) != 0) {}
    if (getpid() < 0) return 1;
    if (getuid() < 0) return 1;
    usleep(1000);
    time_t now = time(0);
    (void)now;

    DIR *d = opendir(".");
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != 0) {
            if (e->d_name[0] == '.') {}
        }
        closedir(d);
    }
    struct passwd *pw = getpwuid(u);
    (void)pw;

    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0) return 1;
    (void)ru.ru_utime.tv_sec;
    (void)ru.ru_stime.tv_usec;

    long long v = strtoll("42", 0, 0);
    unsigned long long uv = strtoull("42", 0, 0);
    (void)uv;
    int a = 0, b = 0;
    int rc = sscanf("| size 4096 pagesize 4096", "| size %d pagesize %d", &a, &b);
    unsigned int h1 = 0, h2 = 0;
    int rc2 = sscanf("| 12: 1f 2f", "| %d: %x %x", &a, &h1, &h2);
    (void)rc; (void)rc2;
    double x = ceil(1.1) + floor(1.9) + log(2.0) + pow(2.0, 3.0);
    (void)x;

    /* file.h 新能力: 池回收 + 定位 */
    FILE *f1 = fopen("/tmp/t1", "w");
    FILE *f2 = fopen("/tmp/t2", "w");
    if (f1 && f2) {
        fputs("hello world\n", f1);
        fclose(f1);
        f1 = 0;
        FILE *f3 = fopen("/tmp/t3", "w");   /* 应复用 f1 的槽位 */
        if (f3) {
            fputs("reused\n", f3);
            fclose(f3);
        }
        fseek(f2, 0, SEEK_SET);
        long pos = ftell(f2);
        (void)pos;
        rewind(f2);
        fputc('x', f2);
        fflush(f2);
        fclose(f2);
        FILE *fr = fopen("/tmp/t1", "r");
        if (fr) {
            int c = fgetc(fr);
            ungetc(c, fr);
            c = fgetc(fr);
            (void)c;
            fclose(fr);
        }
    }
    return (int)(v + a + b + (int)st.st_mode);
}
