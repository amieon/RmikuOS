// su [name] — 切换身份（默认切到 root）
//
// 权限规则：
//   - root（euid==0）免密切换到任何账户
//   - 非 root 必须输入**目标账户**的口令（与 Linux su 一致：不是输自己的口令）
//
// 账户读取走 accounts.h 的 defaults + overrides（/var/etc/passwd 优先，回退 /etc/passwd）。

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

/* 收集附加组（/var/etc/group 优先） */
static int initgroups(const char *name) {
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

static int ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) return 0;
    return (int) mkdir(path, 0777);
}

int main(int argc, char *argv[]) {
    const char *target = (argc >= 2 && argv[1][0]) ? argv[1] : "root";

    struct User u;
    if (!get_user(target, &u)) {
        puts("su: unknown user: ");
        puts(target);
        putchar('\n');
        return 1;
    }

    /* root 免密；否则校验目标账户口令 */
    if (geteuid() != 0) {
        char pass[64];
        puts("Password: ");
        if (acct_readline(pass, (int) sizeof pass) <= 0) return 1;
        char hex[65];
        acct_hash(u.salt, pass, hex);
        if (strcmp(hex, u.hash) != 0) {
            puts("su: incorrect password\n");
            return 1;
        }
    }

    ensure_dir("/home");
    ensure_dir(u.home);

    isize pc = fork();
    if (pc == 0) {
        if (geteuid() == 0) initgroups(u.name);
        setgid(u.gid);
        setuid(u.uid);
        chdir(u.home);
        exec("/bin/shell");
        puts("[su] exec /bin/shell failed\n");
        exit(1);
    } else if (pc > 0) {
        int code = -1;
        waitpid(pc, &code, 0);
        return 0;
    } else {
        puts("[su] fork failed\n");
        return 1;
    }
}
