// groupadd <name> [gid] — 新建组（仅 root）
//
// 存储: defaults + overrides（见 accounts.h）
//   读 /var/etc/group 优先 → 回退 /etc/group；写永远落 /var/etc/group 并 fsync
// 行格式: group_name:gid:members（members 为逗号分隔的用户名）
// 新建组成员为空，用 `usermod -aG <group> <user>` 添加。
//
// 为什么必须 root: 能把任意用户塞进任意组 = 间接提权（例如塞进 root 组）。

#include "user.h"
#include "string.h"
#include "accounts.h"

static int max_gid(const char *db) {
    int best = 0;
    int off = 0;
    char line[ACCT_LINE_MAX];
    while ((off = acct_next_line(db, off, line, (int) sizeof line)) >= 0) {
        if (line[0] == 0 || line[0] == '#') continue;
        char *f[4];
        int nf = acct_fields(line, f, 4);
        if (nf < 2) continue;
        int v = atoi(f[1]);
        if (v > best) best = v;
    }
    return best;
}

static int group_exists(const char *db, const char *name) {
    int off = 0;
    char line[ACCT_LINE_MAX];
    while ((off = acct_next_line(db, off, line, (int) sizeof line)) >= 0) {
        if (line[0] == 0 || line[0] == '#') continue;
        char *f[4];
        int nf = acct_fields(line, f, 4);
        if (nf >= 1 && strcmp(f[0], name) == 0) return 1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        puts("用法: groupadd <name> [gid]\n");
        puts("  加成员: usermod -aG <group> <user>\n");
        exit(1);
    }
    if (getuid() != 0) {
        puts("groupadd: 仅 root 可以新建组\n");
        exit(1);
    }

    const char *name = argv[1];
    if (!acct_name_ok(name)) {
        puts("groupadd: 组名非法(1..31 字节, 不含 : / ,)\n");
        exit(1);
    }

    static char db[ACCT_DB_MAX];
    int len = acct_load(db, (int) sizeof db, ACCT_GROUP_STATE, ACCT_GROUP_DEFAULT);
    if (len < 0) { puts("groupadd: 读不到组库\n"); exit(1); }
    if (group_exists(db, name)) {
        puts("groupadd: 组已存在: ");
        puts(name);
        puts("\n");
        exit(1);
    }

    int gid = (argc > 2) ? atoi(argv[2]) : (max_gid(db) + 1);
    if (gid <= 0) { puts("groupadd: gid 非法\n"); exit(1); }

    /* 组装一行: name:gid:（成员为空） */
    char entry[ACCT_LINE_MAX];
    int at = 0;
    at = acct_append(entry, at, (int) sizeof entry, name);
    at = acct_append(entry, at, (int) sizeof entry, ":");
    at = acct_put_uint(entry, at, (int) sizeof entry, (unsigned int) gid);
    at = acct_append(entry, at, (int) sizeof entry, ":");

    int nlen = acct_append_line(db, len, (int) sizeof db, entry);
    if (nlen < 0) { puts("groupadd: 组库已满\n"); exit(1); }
    if (acct_store(ACCT_GROUP_STATE, db, nlen) < 0) {
        puts("groupadd: 写 ");
        puts(ACCT_GROUP_STATE);
        puts(" 失败\n");
        exit(1);
    }

    puts("groupadd: 已创建组 ");
    puts(name);
    puts(" (gid=");
    printf("%d", gid);
    puts("), 成员为空\n已写入 ");
    puts(ACCT_GROUP_STATE);
    puts(" (已 fsync)\n");
    exit(0);
    return 0;
}
