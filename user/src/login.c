#include "user.h"
#include "string.h"
#include "accounts.h"

struct User {
    char  name[32];
    usize uid, gid;
    char  home[64];
    char  salt[32];
    char  hash[65];
};

/* 按用户名查账户（覆盖层优先） */
static int get_user(const char *name, struct User *out) {
    static char db[ACCT_DB_MAX];
    if (acct_load(db, (int) sizeof db, ACCT_PASSWD_STATE, ACCT_PASSWD_DEFAULT) < 0) return 0;

    int off = 0;
    char line[ACCT_LINE_MAX];
    while ((off = acct_next_line(db, off, line, (int) sizeof line)) >= 0) {
        if (line[0] == 0 || line[0] == '#') continue;
        char *f[8];
        int nf = acct_fields(line, f, 8);
        if (nf < 6 || strcmp(f[0], name) != 0) continue;

        acct_copy(out->name, f[0], (int) sizeof out->name);
        out->uid = (usize) atoi(f[1]);
        out->gid = (usize) atoi(f[2]);
        acct_copy(out->home, f[3], (int) sizeof out->home);
        acct_copy(out->salt, f[4], (int) sizeof out->salt);
        acct_copy(out->hash, f[5], (int) sizeof out->hash);
        return 1;
    }
    return 0;
}

/* 收集附加组并写入内核（/var/etc/group 优先，回退 /etc/group） */
static int initgroups(const char *name, usize primary_gid) {
    (void) primary_gid;                 /* 主组由 setgid 设置，这里只管附加组 */
    static char db[ACCT_DB_MAX];
    if (acct_load(db, (int) sizeof db, ACCT_GROUP_STATE, ACCT_GROUP_DEFAULT) < 0) return 0;

    usize groups[32];
    int ng = 0;
    int off = 0;
    char line[ACCT_LINE_MAX];
    while ((off = acct_next_line(db, off, line, (int) sizeof line)) >= 0) {
        if (line[0] == 0 || line[0] == '#') continue;
        char *f[4];
        int nf = acct_fields(line, f, 4);
        if (nf < 3) continue;
        if (acct_member_has(f[2], name) && ng < 32) {
            groups[ng++] = (usize) atoi(f[1]);
        }
    }
    setgroups((usize) ng, groups);
    return ng;
}

/* 目录不存在则创建 */
static int ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) return 0;
    return (int) mkdir(path, 0777);
}

static isize g_ntp_pid = -1;

static void spawn_ntp_sync(void) {
    isize p = fork();
    if (p == 0) {
        /* 子进程独立成组：不跟 login 共享前台，Ctrl+C 也打不到它 */
        setpgid(0, 0);
        char *const av[] = { "/bin/ntpdate", "-b", 0 };
        execv("/bin/ntpdate", av);
        exit(0);   /* exec 失败也别在登录界面上报错：校时是锦上添花 */
    }
    if (p > 0) {
        g_ntp_pid = p;

        /* 必须抢回前台，否则整个登录界面会被挂起 */
        set_front(getpgrp());
    }
}

/* 收掉已经退出的校时子进程。只认 g_ntp_pid 这一个，绝不用 waitpid(-1)：
 * 那会把 shell 也一起收割掉（login 靠 waitpid(pc) 等 shell 退出）。 */
static void reap_ntp_sync(void) {
    if (g_ntp_pid <= 0) return;
    int code = 0;
    isize r = waitpid(g_ntp_pid, &code, WNOHANG);
    /* r == pid：刚收掉。r < 0：ECHILD/ESRCH，说明已经不在表里了 —— 两种都清掉，
     * 只有 r == 0（还在跑）才保留。留着无效 pid 会让每轮提示符都白跑一次系统调用。 */
    if (r == g_ntp_pid || r < 0) g_ntp_pid = -1;
}

int main(void) {
    char name[64], pass[64], hex[65];

    spawn_ntp_sync();

    for (;;) {
        reap_ntp_sync();
        puts("RmikuOS login: ");
        if (acct_readline(name, (int) sizeof name) <= 0) continue;
        puts("Password: ");
        if (acct_readline(pass, (int) sizeof pass) <= 0) continue;

        struct User u;
        if (!get_user(name, &u)) { puts("login: unknown user\n"); continue; }

        /* 校验 SHA-256(salt ‖ 口令) */
        acct_hash(u.salt, pass, hex);
        if (strcmp(hex, u.hash) != 0) { puts("login: incorrect password\n"); continue; }

        /* 建家目录并归属该用户（此刻仍是 root，可 chown） */
        ensure_dir("/home");
        ensure_dir(u.home);
        chown(u.home, u.uid, u.gid);

        /* 认证成功：子进程降权后 exec shell；父进程回到登录循环 */
        isize pc = fork();
        if (pc == 0) {
            initgroups(u.name, u.gid);   /* 仍为 root 时设置附加组 */
            setgid(u.gid);               /* 先 setgid（此时还是 root） */
            setuid(u.uid);               /* 再 setuid，euid 降为非 0 */
            chdir(u.home);               /* 家目录（不存在则留在 /） */
            setenv("HOME", u.home, 1);
            setenv("PWD", u.home, 1);
            setenv("USER", u.name, 1);
            setenv("LOGNAME", u.name, 1);
            exec("/bin/shell");
            puts("[login] exec /bin/shell failed\n");
            exit(1);
        } else if (pc > 0) {
            int code = -1;
            waitpid(pc, &code, 0);
        } else {
            puts("[login] fork failed\n");
        }
    }
    return 0;
}
