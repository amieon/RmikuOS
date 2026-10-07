// usermod — 修改用户属性(仅 root)
//
// 当前支持:
//   usermod -aG <group> <user>   把用户加进附加组(改 /var/etc/group 的 members)
//   usermod -g  <gid>   <user>   改用户主组(改 /var/etc/passwd 的 gid 字段)
//
// 权限: 只有 root 能改别人的身份。普通用户改自己的口令请用 passwd
//      (passwd 是唯一允许非 root 调用的身份类命令, 且必须先验证旧口令)。
//
// 存储: defaults + overrides —— 读覆盖层优先, 写永远落 /var/etc/ 并 fsync。

#include "user.h"
#include "string.h"

#define DB_SIZE 4096
#define GROUP_STATE "/var/etc/group"
#define GROUP_DEFAULT "/etc/group"
#define PASSWD_STATE "/var/etc/passwd"
#define PASSWD_DEFAULT "/etc/passwd"

static char db[DB_SIZE];
static char out[DB_SIZE];

static int load_into(char *dst, const char *state, const char *dflt) {
    int fd = (int) open(state, O_RDONLY);
    if (fd < 0) fd = (int) open(dflt, O_RDONLY);
    if (fd < 0) return -1;
    int len = 0;
    for (;;) {
        int n = (int) read(fd, dst + len, DB_SIZE - 1 - len);
        if (n <= 0) break;
        len += n;
        if (len >= DB_SIZE - 1) break;
    }
    close(fd);
    dst[len] = 0;
    return len;
}

static int save_out(const char *state, int len) {
    mkdir("/var/etc", 0777);
    int fd = (int) open(state, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    int off = 0;
    while (off < len) {
        isize n = write(fd, out + off, (usize)(len - off));
        if (n <= 0) break;
        off += (int) n;
    }
    fsync(fd);
    close(fd);
    return (off == len) ? 0 : -1;
}

static int streq(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == 0 && *b == 0;
}

/* members 里是否已有该用户(逗号分隔精确匹配) */
static int members_have(const char *members, const char *user) {
    const char *p = members;
    while (*p) {
        const char *q = p;
        while (*q && *q != ',') q++;
        int n = (int)(q - p);
        int hit = 1;
        if ((int)strlen(user) != n) hit = 0;
        else for (int i = 0; i < n; i++) if (p[i] != user[i]) { hit = 0; break; }
        if (hit) return 1;
        if (*q == 0) break;
        p = q + 1;
    }
    return 0;
}

/* -aG: 把 user 追加进 group 的 members */
static int add_to_group(const char *group, const char *user) {
    int len = load_into(db, GROUP_STATE, GROUP_DEFAULT);
    if (len < 0) { puts("usermod: 读不到组库\n"); return -1; }

    int at = 0;
    int changed = 0;
    int gfound = 0;
    char *line = db;
    while (*line) {
        char *nl = line;
        while (*nl && *nl != '\n') nl++;
        char saved = *nl;
        *nl = 0;

        if (line[0] && line[0] != '#') {
            char *f[4];
            int nf = 0;
            f[nf++] = line;
            for (char *p = line; *p && nf < 4; p++) {
                if (*p == ':') { *p = '\0'; f[nf++] = p + 1; }
            }
            if (nf >= 2 && streq(f[0], group)) {
                gfound = 1;
                const char *members = (nf >= 3) ? f[2] : "";
                if (members_have(members, user)) {
                    puts("usermod: 用户已在组中, 无需修改\n");
                    *nl = saved;
                    return 0;
                }
                /* name:gid:旧成员[,新用户] */
                for (int i = 0; f[0][i]; i++) out[at++] = f[0][i];
                out[at++] = ':';
                for (int i = 0; f[1][i]; i++) out[at++] = f[1][i];
                out[at++] = ':';
                for (int i = 0; members[i]; i++) out[at++] = members[i];
                if (members[0]) out[at++] = ',';
                for (int i = 0; user[i]; i++) out[at++] = user[i];
                out[at++] = '\n';
                changed = 1;
            } else {
                for (char *p = line; *p; p++) out[at++] = *p;
                out[at++] = '\n';
            }
        } else {
            for (char *p = line; *p; p++) out[at++] = *p;
            out[at++] = '\n';
        }

        if (saved == 0) break;
        *nl = saved;
        line = nl + 1;
    }
    out[at] = 0;

    if (!gfound) {
        puts("usermod: 组不存在: ");
        puts(group);
        puts(" (先用 groupadd 建组)\n");
        return -1;
    }
    if (!changed) return 0;
    if (save_out(GROUP_STATE, at) < 0) { puts("usermod: 写组库失败\n"); return -1; }
    return 0;
}

/* -g: 改 user 的主组 */
static int set_primary_gid(const char *user, const char *gidstr) {
    int len = load_into(db, PASSWD_STATE, PASSWD_DEFAULT);
    if (len < 0) { puts("usermod: 读不到密码库\n"); return -1; }

    int at = 0;
    int changed = 0;
    char *line = db;
    while (*line) {
        char *nl = line;
        while (*nl && *nl != '\n') nl++;
        char saved = *nl;
        *nl = 0;

        if (line[0] && line[0] != '#') {
            char *f[8];
            int nf = 0;
            f[nf++] = line;
            for (char *p = line; *p && nf < 8; p++) {
                if (*p == ':') { *p = '\0'; f[nf++] = p + 1; }
            }
            if (nf >= 6 && streq(f[0], user)) {
                /* name:uid:新gid:home:salt:hash */
                for (int i = 0; f[0][i]; i++) out[at++] = f[0][i];
                out[at++] = ':';
                for (int i = 0; f[1][i]; i++) out[at++] = f[1][i];
                out[at++] = ':';
                for (int i = 0; gidstr[i]; i++) out[at++] = gidstr[i];
                out[at++] = ':';
                for (int i = 0; f[3][i]; i++) out[at++] = f[3][i];
                out[at++] = ':';
                for (int i = 0; f[4][i]; i++) out[at++] = f[4][i];
                out[at++] = ':';
                for (int i = 0; f[5][i]; i++) out[at++] = f[5][i];
                out[at++] = '\n';
                changed = 1;
            } else {
                for (char *p = line; *p; p++) out[at++] = *p;
                out[at++] = '\n';
            }
        } else {
            for (char *p = line; *p; p++) out[at++] = *p;
            out[at++] = '\n';
        }

        if (saved == 0) break;
        *nl = saved;
        line = nl + 1;
    }
    out[at] = 0;

    if (!changed) {
        puts("usermod: 用户不存在: ");
        puts(user);
        puts("\n");
        return -1;
    }
    if (save_out(PASSWD_STATE, at) < 0) { puts("usermod: 写密码库失败\n"); return -1; }
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        puts("用法:\n");
        puts("  usermod -aG <group> <user>   加入附加组\n");
        puts("  usermod -g  <gid>   <user>   改主组\n");
        exit(1);
    }
    if (getuid() != 0) {
        puts("usermod: 仅 root 可以修改用户属性\n");
        exit(1);
    }

    const char *opt = argv[1];
    if (streq(opt, "-aG")) {
        if (add_to_group(argv[2], argv[3]) < 0) exit(1);
        puts("usermod: 已把 ");
        puts(argv[3]);
        puts(" 加入组 ");
        puts(argv[2]);
        puts("\n");
        exit(0);
    } else if (streq(opt, "-g")) {
        if (set_primary_gid(argv[3], argv[2]) < 0) exit(1);
        puts("usermod: 已修改 ");
        puts(argv[3]);
        puts(" 的主组为 ");
        puts(argv[2]);
        puts("\n");
        exit(0);
    }

    puts("usermod: 未知选项: ");
    puts(opt);
    puts("\n");
    exit(1);
}
