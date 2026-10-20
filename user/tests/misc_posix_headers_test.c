/*
 * misc_posix_headers_test.c —— 新补的 POSIX 头文件 + libc 垫片
 *
 * 覆盖 14 个头（strings/syslog/grp/alloca/sys.param/sys.wait/sys.poll/
 * sys.select/sys.socket/sys.utsname/netinet.in/netinet.tcp/arpa.inet/netdb）。
 *
 * 重点不是"能编过"，而是**语义对不对**：
 *   - strcasecmp 按 unsigned char 比较（高位字节不能排到 ASCII 前面）
 *   - inet_pton 必须拒绝 "256.1.1.1" / "1.2.3" / 带尾随字符的输入
 *   - select 必须真的等价于 poll（空管道超时 0、写入后可读 1）
 *   - wait status 按本内核的 128+sig 编码判断，而不是 Linux 的位掩码
 */
#include "test.h"

#include <strings.h>
#include <unistd.h>        /* gethostname / sysconf / alarm */
#include <stdlib.h>        /* bsearch */
#include <pwd.h>           /* getpwnam */
#include <signal.h>        /* sigaction */
#include <libgen.h>        /* basename / dirname */
#include <sys/resource.h>  /* getrlimit */
#include <syslog.h>
#include <grp.h>
#include <alloca.h>
#include <sys/param.h>
#include <sys/wait.h>
#include <sys/poll.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <dirent.h>        /* DT_* / d_type */
#include <sys/un.h>        /* sockaddr_un */
#include <sys/sysinfo.h>   /* struct sysinfo */
#include <endian.h>        /* __BYTE_ORDER */
#include <stddef.h>        /* offsetof */

static int cmp_int(const void *a, const void *b) {
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}

int main(void) {
    TEST_START("misc_posix_headers");

    /* ---------- 1. strings.h: strcasecmp / strncasecmp ---------- */
    CHECK(strcasecmp("Hello", "hELLo") == 0, "strcasecmp 忽略大小写");
    CHECK(strcasecmp("abc", "abd") < 0, "strcasecmp 顺序正确");
    CHECK(strcasecmp("", "") == 0, "strcasecmp 空串相等");
    CHECK(strncasecmp("Hello", "help", 3) == 0, "strncasecmp 只比前 3 字节");
    CHECK(strncasecmp("Hello", "help", 4) != 0, "strncasecmp 第 4 字节不同");

    /* 高位字节必须按 unsigned 解释：0xFF 不能小于 'a'(0x61) */
    CHECK(strcasecmp("\xff", "a") > 0, "strcasecmp 按 unsigned char 比较");

    /* ---------- 2. arpa/inet.h: pton / ntop ---------- */
    unsigned char ip[4] = {0, 0, 0, 0};
    CHECK_EQ(inet_pton(AF_INET, "192.168.1.1", ip), 1, "inet_pton 返回 1");
    CHECK(ip[0] == 192 && ip[1] == 168 && ip[2] == 1 && ip[3] == 1,
          "inet_pton 得到 192.168.1.1");

    CHECK_EQ(inet_pton(AF_INET, "256.1.1.1", ip), 0, "inet_pton 拒绝 256");
    CHECK_EQ(inet_pton(AF_INET, "1.2.3", ip), 0, "inet_pton 拒绝段数不足");
    CHECK_EQ(inet_pton(AF_INET, "1.2.3.4x", ip), 0, "inet_pton 拒绝尾随字符");

    char text[16];
    unsigned char loop[4] = {127, 0, 0, 1};
    CHECK(inet_ntop(AF_INET, loop, text, sizeof text) != 0, "inet_ntop 成功");
    CHECK_STREQ(text, "127.0.0.1", "inet_ntop 得到 127.0.0.1");

    unsigned char zero[4] = {0, 0, 0, 0};
    inet_ntop(AF_INET, zero, text, sizeof text);
    CHECK_STREQ(text, "0.0.0.0", "inet_ntop 处理 0 段");

    char tiny[4];
    CHECK(inet_ntop(AF_INET, loop, tiny, sizeof tiny) == 0, "inet_ntop 缓冲区不足返回 NULL");

    CHECK(inet_addr("10.0.2.2") == htonl(0x0A000202u), "inet_addr 返回网络序");
    CHECK(inet_addr("bad") == INADDR_NONE, "inet_addr 非法输入返回 INADDR_NONE");

    struct in_addr ia;
    ia.s_addr = htonl(0x7F000001u);
    CHECK_STREQ(inet_ntoa(ia), "127.0.0.1", "inet_ntoa 正确");

    /* ---------- 3. sys/utsname.h ---------- */
    struct utsname u;
    CHECK_EQ(uname(&u), 0, "uname() 成功");
    CHECK_STREQ(u.sysname, "RmikuOS", "sysname = RmikuOS");
    CHECK_STREQ(u.nodename, "rmikuos", "nodename = rmikuos");
    CHECK(strstr(u.version, "RmikuOS") != 0, "version 含 RmikuOS");

    /* ---------- 4. unistd.h: gethostname ---------- */
    char host[32];
    CHECK_EQ(gethostname(host, sizeof host), 0, "gethostname 成功");
    CHECK_STREQ(host, "rmikuos", "gethostname 与 nodename 一致");
    CHECK_EQ(gethostname(host, 0), -1, "gethostname(len=0) 失败");

    /* ---------- 5. sys/select.h: fd_set 位运算 ---------- */
    fd_set rs;
    FD_ZERO(&rs);
    FD_SET(0, &rs);
    FD_SET(200, &rs);
    CHECK(FD_ISSET(0, &rs) != 0, "FD_SET/FD_ISSET fd=0");
    CHECK(FD_ISSET(200, &rs) != 0, "FD_SET/FD_ISSET fd=200(跨字边界)");
    CHECK(FD_ISSET(1, &rs) == 0, "未设置的 fd 不可见");
    FD_CLR(0, &rs);
    CHECK(FD_ISSET(0, &rs) == 0, "FD_CLR 清位");

    /* ---------- 6. select 真的等价于 poll ---------- */
    int fds[2] = {-1, -1};
    CHECK_EQ(pipe(fds), 0, "pipe() 成功");
    if (fds[0] >= 0 && fds[1] >= 0) {
        fd_set r;
        FD_ZERO(&r);
        FD_SET(fds[0], &r);
        struct timeval tv0 = {0, 0};

        CHECK_EQ(select(fds[0] + 1, &r, 0, 0, &tv0), 0, "空管道 select 超时返回 0");

        CHECK_EQ(write(fds[1], "x", 1), 1, "写入 1 字节");
        FD_ZERO(&r);
        FD_SET(fds[0], &r);
        struct timeval tv1 = {1, 0};
        int n = select(fds[0] + 1, &r, 0, 0, &tv1);
        CHECK_EQ(n, 1, "写入后 select 返回 1");
        CHECK(FD_ISSET(fds[0], &r) != 0, "写入后 readfds 含该 fd");

        char c = 0;
        CHECK_EQ(read(fds[0], &c, 1), 1, "读回 1 字节");
        close(fds[0]);
        close(fds[1]);
    }

    /* nfds 超上限必须明确失败，而不是悄悄截断 */
    fd_set r2;
    FD_ZERO(&r2);
    CHECK_EQ(select(FD_SETSIZE + 1, &r2, 0, 0, 0), -1, "nfds > FD_SETSIZE 返回 -1");

    /* ---------- 7. netdb.h: 失败实现 ---------- */
    struct addrinfo *ai = (struct addrinfo *)0x1;
    CHECK_EQ(getaddrinfo("example.com", 0, 0, &ai), EAI_FAIL, "getaddrinfo 明确失败");
    CHECK(ai == (struct addrinfo *)0, "getaddrinfo 不返回伪结果");
    freeaddrinfo(ai);                     /* 必须能接受 NULL */
    CHECK(gethostbyname("localhost") == 0, "gethostbyname 返回 NULL");
    CHECK(strcmp(gai_strerror(EAI_FAIL), "non-recoverable failure") == 0,
          "gai_strerror 有对应文本");

    /* ---------- 8. grp.h / syslog.h / alloca.h / sys/param.h ---------- */
    CHECK(getgrnam("root") == 0, "getgrnam 当前返回 NULL(未接账户库)");
    openlog("test", LOG_PID, LOG_USER);
    syslog(LOG_INFO, "value=%d", 42);     /* 空操作，只要编过 */
    closelog();
    CHECK(1, "syslog 宏系列可用");

    char *buf = (char *)alloca(32);
    for (int i = 0; i < 31; i++) buf[i] = (char)('a' + (i % 26));
    buf[31] = '\0';
    CHECK(buf[0] == 'a' && buf[30] == (char)('a' + (30 % 26)), "alloca 可用");

    CHECK_EQ(MAXPATHLEN, 1024, "MAXPATHLEN = 1024");
    CHECK_EQ(MIN(3, 7), 3, "MIN 宏");
    CHECK_EQ(howmany(10, 4), 3, "howmany 向上取整");

    /* ---------- 9. sys/wait.h: POSIX 的 16 位 wait status ---------- */
    CHECK(WIFEXITED(0), "status 0 = 正常退出 0");
    CHECK(WIFEXITED(1 << 8), "exit(1) 编码为 0x0100");
    CHECK_EQ(WEXITSTATUS(42 << 8), 42, "WEXITSTATUS 取回 42");
    CHECK(!WIFEXITED(11), "低 7 位非 0 时不判为正常退出");
    CHECK(WIFSIGNALED(11), "11 = 被信号 11 杀死");
    CHECK_EQ(WTERMSIG(0x8b), 11, "0x8b -> 信号 11");
    CHECK(WCOREDUMP(0x8b), "bit 7 = core dump 标记");
    CHECK(WIFSTOPPED(0x137f), "0x137f = 被信号 19 暂停");
    CHECK_EQ(WSTOPSIG(0x137f), 19, "暂停信号取高 8 位");
    CHECK(!WIFSIGNALED(0x7f), "0x7f 是暂停标签,不算信号死亡");
    /* 回归锁：一旦写成 128+sig，exit(139) 与 SIGSEGV 就无法区分了 */
    CHECK(WIFEXITED(139 << 8) && WEXITSTATUS(139 << 8) == 139,
          "exit(139) 仍是正常退出,不与 SIGSEGV 混淆");

    /* ---------- 10. 网络结构体布局 ---------- */
    CHECK_EQ(sizeof(struct sockaddr), 16, "sizeof(struct sockaddr) = 16");
    CHECK_EQ(sizeof(struct in_addr), 4, "sizeof(struct in_addr) = 4");
    CHECK_EQ(sizeof(struct sockaddr_in), 16, "sizeof(struct sockaddr_in) = 16");
    CHECK_EQ(TCP_NODELAY, 1, "TCP_NODELAY 已定义");

    /* ---------- 11. bsearch ---------- */
    int sorted[5] = {1, 3, 5, 7, 9};
    int k5 = 5, k4 = 4, k9 = 9;
    int *hit = (int *)bsearch(&k5, sorted, 5, sizeof(int), cmp_int);
    CHECK(hit != 0 && *hit == 5, "bsearch 找到中间元素");
    CHECK((int *)bsearch(&k9, sorted, 5, sizeof(int), cmp_int) == &sorted[4],
          "bsearch 找到末尾元素并返回数组内地址");
    CHECK((int *)bsearch(&k4, sorted, 5, sizeof(int), cmp_int) == 0,
          "bsearch 未命中返回 NULL");

    /* ---------- 12. sysconf / alarm ---------- */
    CHECK_EQ(sysconf(_SC_PAGESIZE), 4096, "sysconf(_SC_PAGESIZE) = 4096");
    CHECK_EQ(sysconf(_SC_OPEN_MAX), 1024, "sysconf(_SC_OPEN_MAX) = 1024");
    CHECK_GE(sysconf(_SC_NPROCESSORS_ONLN), 1, "sysconf(_SC_NPROCESSORS_ONLN) >= 1");

    errno = 0;
    CHECK_EQ(sysconf(12345), -1, "未知 sysconf 名字返回 -1");
    CHECK_EQ(errno, EINVAL, "未知 sysconf 名字置 EINVAL");

    CHECK_EQ(alarm(5), 0, "alarm 无实现时返回 0(没有未决闹钟)");

    /* ---------- 13. sigaction / sigset / sigprocmask ---------- */
    struct sigaction sa, old;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    CHECK_EQ(sigaction(SIGPIPE, &sa, &old), 0, "sigaction(SIGPIPE, SIG_IGN) 成功");
    CHECK(old.sa_handler == SIG_DFL, "查询到的旧处置是 SIG_DFL");

    /* 自定义 handler 内核装不上：必须明确失败，不能假装成功 */
    sa.sa_handler = (sighandler_t)cmp_int;
    errno = 0;
    CHECK_EQ(sigaction(SIGTERM, &sa, 0), -1, "自定义 handler 明确失败");
    CHECK_EQ(errno, ENOSYS, "自定义 handler 置 ENOSYS");

    sigset_t ss;
    sigemptyset(&ss);
    sigaddset(&ss, SIGPIPE);
    CHECK_EQ(sigismember(&ss, SIGPIPE), 1, "sigaddset / sigismember");
    sigdelset(&ss, SIGPIPE);
    CHECK_EQ(sigismember(&ss, SIGPIPE), 0, "sigdelset");
    CHECK_EQ(sigprocmask(SIG_UNBLOCK, &ss, 0), 0, "sigprocmask 接受并返回 0");

    /* ---------- 14. getpwnam ---------- */
    CHECK(getpwnam("root") == 0, "getpwnam 与 getpwuid 一致(未接账户库返回 NULL)");

    /* ---------- 15. exec 家族的失败语义 ---------- */
    char *nargv[2] = {(char *)"no_such_cmd_xyz", 0};
    errno = 0;
    CHECK_EQ(execvp("no_such_cmd_xyz", nargv), -1, "execvp 命令不存在返回 -1");
    CHECK_EQ(errno, ENOENT, "execvp 命令不存在置 ENOENT");

    char *lots[10];
    for (int i = 0; i < 10; i++) lots[i] = (char *)"x";
    errno = 0;
    CHECK_EQ(execv("/bin/echo", lots), -1, "execv 超过 8 个参数返回 -1");
    CHECK_EQ(errno, E2BIG, "execv 超过 8 个参数置 E2BIG");

    char *envp[1] = {0};
    errno = 0;
    CHECK_EQ(execve("/bin/echo", nargv, envp), -1, "execve 明确失败(无 envp 通道)");
    CHECK_EQ(errno, ENOSYS, "execve 置 ENOSYS");

    /* ---------- 16. _exit 与 execvp 的真实路径(fork 隔离) ---------- */
    fflush(stdout);                    /* 子进程继承缓冲区，先刷掉避免输出重复 */

    int st = 0;
    isize pid = fork();
    if (pid == 0) {
        _exit(7);                      /* 不走 atexit、不刷 stdio */
    }
    CHECK(pid > 0, "fork 成功");
    if (pid > 0) {
        CHECK_EQ(waitpid(pid, &st, 0), pid, "waitpid 收回子进程");
        CHECK(WIFEXITED(st), "子进程判为正常退出");
        CHECK_EQ(WEXITSTATUS(st), 7, "_exit(7) 的退出码被正确取回");
    }

    pid = fork();
    if (pid == 0) {
        char *eargv[3] = {(char *)"echo", (char *)"execvp_ok", 0};
        execvp("echo", eargv);         /* PATH 查找 */
        _exit(99);                     /* 只有 exec 失败才到这里 */
    }
    if (pid > 0) {
        int st2 = 0;
        waitpid(pid, &st2, 0);
        CHECK(WIFEXITED(st2) && WEXITSTATUS(st2) == 0, "execvp(\"echo\") 真的执行成功");
    }

    /* 子进程被信号杀死：内核内部存 128+15，转换后低 7 位应是 15 */
    fflush(stdout);
    pid = fork();
    if (pid == 0) {
        kill(getpid(), SIGTERM);
        _exit(0);                      /* 信号没生效才会走到这里 */
    }
    if (pid > 0) {
        int st3 = 0;
        waitpid(pid, &st3, 0);
        CHECK(WIFSIGNALED(st3), "SIGTERM 致死 -> WIFSIGNALED 成立");
        CHECK_EQ(WTERMSIG(st3), SIGTERM, "WTERMSIG 取回信号号 15");
    }

    /* ---------- 17. libgen.h / getrlimit ---------- */
    char lp1[] = "/usr/lib/git";
    CHECK_STREQ(basename(lp1), "git", "basename(/usr/lib/git)");
    char lp2[] = "/usr/lib/git";
    CHECK_STREQ(dirname(lp2), "/usr/lib", "dirname(/usr/lib/git)");
    char lp3[] = "usr";
    CHECK_STREQ(dirname(lp3), ".", "dirname(usr) = .");
    char lp4[] = "/usr";
    CHECK_STREQ(dirname(lp4), "/", "dirname(/usr) = /");
    char lp5[] = "/";
    CHECK_STREQ(basename(lp5), "/", "basename(/) = /");
    char lp6[] = "a/b/";
    CHECK_STREQ(basename(lp6), "b", "basename 会去掉尾斜杠");

    struct rlimit rl;
    CHECK_EQ(getrlimit(RLIMIT_NOFILE, &rl), 0, "getrlimit(RLIMIT_NOFILE) 成功");
    CHECK_EQ(rl.rlim_cur, 1024, "NOFILE soft limit = 1024");
    CHECK_EQ(getrlimit(RLIMIT_STACK, &rl), 0, "getrlimit(RLIMIT_STACK) 成功");
    CHECK(rl.rlim_cur > 0, "STACK soft limit > 0");
    CHECK_EQ(getrlimit(-1, (struct rlimit *)0), -1, "getrlimit 空指针返回 -1");

    /* ---------- 18. 字节序 / dirent.d_type / mkstemps ----------
     * 这三样都是"为了让 git 编过"才补的，但补的方式各有约束：
     *   - 字节序必须由编译器内建宏决定，不能写死（换大端目标不用改代码）
     *   - dirent 不能加字段（64 字节是内核 ABI），d_type 是借 reserved[0] 的
     *   - mkstemps 的"X 的结束位置"是 len-suffixlen，不是 len
     */
    CHECK(__BYTE_ORDER == __LITTLE_ENDIAN, "两个目标架构都是小端");
    CHECK_EQ((int)sizeof(struct dirent), 64, "struct dirent 仍是 64 字节（内核 ABI 未变）");
    CHECK_EQ((int)offsetof(struct dirent, d_name), 8, "d_name 仍在偏移 8");
    CHECK_EQ((int)offsetof(struct dirent, d_type), 2, "d_type 借用的是 reserved[0] 的位置");
    CHECK_EQ(rk_dtype_of(FILE_TYPE_FILE), DT_REG, "内核 1(文件) -> DT_REG(8)");
    CHECK_EQ(rk_dtype_of(FILE_TYPE_DIR), DT_DIR, "内核 2(目录) -> DT_DIR(4)");
    CHECK_EQ(rk_dtype_of(FILE_TYPE_SYMLINK), DT_LNK, "内核 3(symlink) -> DT_LNK(10)");
    CHECK_EQ(rk_dtype_of(99), DT_UNKNOWN, "不认识的内核类型 -> DT_UNKNOWN");

    /* 模板不合法时必须在碰文件系统之前就失败（这里没有可写目录，正好只测算术） */
    char bad1[] = "abc";
    CHECK_EQ(mkstemps(bad1, 0), -1, "mkstemps 模板不足 6 个字符 -> -1");
    char bad2[] = "abcdef";
    CHECK_EQ(mkstemps(bad2, 2), -1, "mkstemps 后缀太长、放不下 6 个 X -> -1");
    char bad3[] = "abXXXXXc";        /* 5 个 X 不合法 */
    CHECK_EQ(mkstemps(bad3, 1), -1, "mkstemps 中间不是连续 6 个 X -> -1");

    /* ---------- 19. putenv ----------
     * git 在没有 putenv 的平台上会编 compat/setenv.c，那里就是拼
     * "name=value" 整串丢给 putenv。我们的环境在内核里，所以是**拷贝语义**
     * （之后改那个 buffer 不影响环境）——这点与 glibc 不同，但调用方无感。 */
    char penv[] = "RK_PUTENV=hi";
    CHECK_EQ(putenv(penv), 0, "putenv 按 '=' 切分后设置成功");
    char *pv = getenv("RK_PUTENV");
    CHECK(pv && strcmp(pv, "hi") == 0, "putenv 之后 getenv 读回 hi");
    char pbad[] = "NOEQUALS";
    CHECK_EQ(putenv(pbad), -1, "没有 '=' 的字符串 -> EINVAL（POSIX 未定义，不猜）");
    unsetenv_s("RK_PUTENV");         /* 清理，不计断言 */

    TEST_END();
}
