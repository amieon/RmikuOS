// id — 显示当前进程身份: uid / gid / 附加组(任何用户都可运行, 只读)
//
// 与 Linux `id` 的最小子集:直接从内核取(而不是从文件猜),
// 所以能真实反映 login 时 initgroups + setgid + setuid 的结果。
//
// 名字解析: 用 uid/gid 反查 /var/etc/passwd(覆盖层优先) 与 /var/etc/group。
// 注意: setgroups 由内核保存; 若进程未经 login 设置附加组, 这里就是空的。

#include "user.h"
#include "string.h"

#define DB_SIZE 4096
#define PASSWD_STATE "/var/etc/passwd"
#define PASSWD_DEFAULT "/etc/passwd"
#define GROUP_STATE "/var/etc/group"
#define GROUP_DEFAULT "/etc/group"

static char buf[DB_SIZE];

static int load_into(const char *state, const char *dflt) {
    int fd = (int) open(state, O_RDONLY);
    if (fd < 0) fd = (int) open(dflt, O_RDONLY);
    if (fd < 0) return -1;
    int len = 0;
    for (;;) {
        int n = (int) read(fd, buf + len, DB_SIZE - 1 - len);
        if (n <= 0) break;
        len += n;
        if (len >= DB_SIZE - 1) break;
    }
    close(fd);
    buf[len] = 0;
    return len;
}

/* 在冒号分隔文件里按字段值查找, 取第 0 字段(名字)。
   which==0 查 passwd(第1字段=uid); which==1 查 group(第1字段=gid) */
static void name_of(int which, unsigned int key, char *dst) {
    dst[0] = 0;
    int rc = (which == 0)
        ? load_into(PASSWD_STATE, PASSWD_DEFAULT)
        : load_into(GROUP_STATE, GROUP_DEFAULT);
    if (rc < 0) return;

    char *line = buf;
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
            if (nf >= 2 && (unsigned int) atoi(f[1]) == key) {
                int i = 0;
                for (; f[0][i] && i < 31; i++) dst[i] = f[0][i];
                dst[i] = 0;
                *nl = saved;
                return;
            }
        }
        if (saved == 0) break;
        *nl = saved;
        line = nl + 1;
    }
}

int main(void) {
    char uname[32], gname[32];
    usize uid = (usize) getuid();
    usize gid = (usize) getgid();

    name_of(0, (unsigned int) uid, uname);
    name_of(1, (unsigned int) gid, gname);

    puts("uid=");
    printf("%d", (int) uid);
    puts("(");
    puts(uname[0] ? uname : "?");
    puts(") gid=");
    printf("%d", (int) gid);
    puts("(");
    puts(gname[0] ? gname : "?");
    puts(")");

    /* 附加组: 先问数量, 再取列表 */
    isize n = getgroups(0, (usize *) 0);
    if (n > 0) {
        usize list[32];
        int cnt = (int)((n < 32) ? n : 32);
        isize got = getgroups((usize) cnt, list);
        if (got > 0) {
            puts(" groups=");
            for (int i = 0; i < (int)((got < 32) ? got : 32); i++) {
                char gn[32];
                name_of(1, (unsigned int) list[i], gn);
                if (i) puts(",");
                printf("%d", (int) list[i]);
                puts("(");
                puts(gn[0] ? gn : "?");
                puts(")");
            }
        }
    }
    puts("\n");
    exit(0);
    return 0;
}
