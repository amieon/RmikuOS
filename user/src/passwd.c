// passwd <name> — 修改用户口令
//
// 权限规则(POSIX 简化版):
//   - 非 root: 必须先输入正确的旧口令, 才能改(任何用户名都走这一步,
//              由调用者自己保证改的是自己的账户)
//   - root   : 免旧口令, 直接设新口令
//
// 存储: 与 useradd 同一套 defaults + overrides:
//   读 /var/etc/passwd 优先, 回退 /etc/passwd; 写永远落在 /var/etc/passwd 并 fsync。
// 口令变更会**重新生成盐**(截断旧 hash 的彩虹表复用), 与出厂账户格式一致。

#include "user.h"
#include "string.h"
#include "SHA256.h"

#define DB_SIZE 4096
#define DB_STATE "/var/etc/passwd"
#define DB_DEFAULT "/etc/passwd"

static char db[DB_SIZE];
static char out[DB_SIZE];

static int load_db(void) {
    int fd = (int) open(DB_STATE, O_RDONLY);
    if (fd < 0) fd = (int) open(DB_DEFAULT, O_RDONLY);
    if (fd < 0) return -1;
    int len = 0;
    for (;;) {
        int n = (int) read(fd, db + len, DB_SIZE - 1 - len);
        if (n <= 0) break;
        len += n;
        if (len >= DB_SIZE - 1) break;
    }
    close(fd);
    db[len] = 0;
    return len;
}

static int save_db(int len) {
    mkdir("/var/etc", 0777);
    int fd = (int) open(DB_STATE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
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

/* 读一行口令(回显; 与 login 当前行为一致, 终端 raw mode 是后续改进项) */
static int readline(char *buf, int max) {
    int i = 0;
    for (;;) {
        int c = getchar();
        if (c < 0) return -1;
        if (c == '\n' || c == '\r') { buf[i] = 0; return i; }
        if (i < max - 1) buf[i++] = (char) c;
    }
}

static int append_uint(char *buf, int at, unsigned int v) {
    char tmp[16];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v > 0) { tmp[n++] = (char)('0' + (v % 10u)); v /= 10u; }
    for (int i = 0; i < n; i++) buf[at + i] = tmp[n - 1 - i];
    return n;
}

/* 计算 SHA-256(salt ‖ 口令) 的十六进制 */
static void hash_password(const char *salt, const char *pass, char *hex) {
    char blob[256];
    int total = 0;
    for (int i = 0; salt[i]; i++) blob[total++] = salt[i];
    for (int i = 0; pass[i]; i++) blob[total++] = pass[i];
    unsigned char dig[32];
    sha256((const unsigned char *)blob, (usize) total, dig);
    to_hex(dig, hex);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        puts("用法: passwd <name>\n");
        puts("  非 root 需先通过旧口令校验; root 可直接设新口令\n");
        exit(1);
    }
    const char *name = argv[1];

    int len = load_db();
    if (len < 0) { puts("passwd: 读不到密码库\n"); exit(1); }

    /* 第一遍: 定位该用户行, 取出 salt / hash 用于旧口令校验 */
    char old_salt[64];
    char old_hash[80];
    int found = 0;

    char *line = db;
    while (*line) {
        char *nl = line;
        while (*nl && *nl != '\n') nl++;
        char saved = *nl;
        *nl = 0;
        int hit = 0;
        if (line[0] && line[0] != '#') {
            char *f[8];
            int nf = 0;
            f[nf++] = line;
            for (char *p = line; *p && nf < 8; p++) {
                if (*p == ':') { *p = '\0'; f[nf++] = p + 1; }
            }
            if (nf >= 6 && strcmp(f[0], name) == 0) {
                hit = 1;
                found = 1;
                int i = 0;
                for (; f[4][i] && i < 63; i++) old_salt[i] = f[4][i];
                old_salt[i] = 0;
                i = 0;
                for (; f[5][i] && i < 79; i++) old_hash[i] = f[5][i];
                old_hash[i] = 0;
            }
        }
        *nl = saved;
        if (hit) break;
        if (saved == 0) break;
        line = nl + 1;
    }

    if (!found) {
        puts("passwd: 未知用户: ");
        puts(name);
        puts("\n");
        exit(1);
    }

    /* 非 root: 校验旧口令 */
    if (getuid() != 0) {
        char old[64], hex[65];
        puts("旧口令: ");
        if (readline(old, sizeof(old)) < 0) { puts("\n"); exit(1); }
        puts("\n");
        hash_password(old_salt, old, hex);
        if (strcmp(hex, old_hash) != 0) {
            puts("passwd: 旧口令不正确\n");
            exit(1);
        }
    }

    char np[64];
    puts("新口令: ");
    if (readline(np, sizeof(np)) <= 0) { puts("passwd: 口令不能为空\n"); exit(1); }
    puts("\n");

    /* 重新生成盐(与出厂格式一致) */
    char salt[32];
    int sl = 0;
    for (const char *s = "s4lt_"; *s; s++) salt[sl++] = *s;
    for (int i = 0; name[i] && i < 4; i++) salt[sl++] = name[i];
    salt[sl++] = '_';
    sl += append_uint(salt, sl, (unsigned int) getpid());
    salt[sl] = 0;

    char hex[65];
    hash_password(salt, np, hex);

    /* 第二遍: 重建整库, 目标行用新 salt/hash */
    int at = 0;
    int changed = 0;
    line = db;
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
            if (nf >= 6 && strcmp(f[0], name) == 0) {
                /* name:uid:gid:home:new_salt:new_hash */
                for (int i = 0; f[0][i]; i++) out[at++] = f[0][i];
                out[at++] = ':';
                for (int i = 0; f[1][i]; i++) out[at++] = f[1][i];
                out[at++] = ':';
                for (int i = 0; f[2][i]; i++) out[at++] = f[2][i];
                out[at++] = ':';
                for (int i = 0; f[3][i]; i++) out[at++] = f[3][i];
                out[at++] = ':';
                for (int i = 0; i < sl; i++) out[at++] = salt[i];
                out[at++] = ':';
                for (int i = 0; hex[i]; i++) out[at++] = hex[i];
                out[at++] = '\n';
                changed = 1;
            } else {
                /* 原样拷贝(字段分隔符已被改成 \0, 按原字节流复制) */
                for (char *p = line; *p; p++) out[at++] = *p;
                out[at++] = '\n';
            }
        } else {
            for (char *p = line; *p; p++) out[at++] = *p;   /* 注释行原样保留 */
            out[at++] = '\n';
        }

        if (saved == 0) break;
        *nl = saved;
        line = nl + 1;
    }
    out[at] = 0;

    if (!changed) { puts("passwd: 未找到目标行(库可能已损坏)\n"); exit(1); }

    if (save_db(at) < 0) {
        puts("passwd: 写 ");
        puts(DB_STATE);
        puts(" 失败\n");
        exit(1);
    }

    puts("passwd: 已更新 ");
    puts(name);
    puts(" 的口令(新盐 + 新 hash, 已 fsync)\n");
    exit(0);
    return 0;
}
