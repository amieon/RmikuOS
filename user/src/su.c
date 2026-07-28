#include "user.h"
#include "SHA256.h"

struct User {
    const char *name;
    usize uid, gid;
    const char *home;
    const char *salt;
    const char *hash;
};

/* 把 line 按 ':' 拆成最多 maxf 个字段, 返回字段数(字段指针指向 line 内部,
   分隔符处被改写为 '\0')。 */
static int split_colon(char *line, char *fields[], int maxf) {
    int n = 0;
    fields[n++] = line;
    for (char *p = line; *p && n < maxf; p++) {
        if (*p == ':') { *p = '\0'; fields[n++] = p + 1; }
    }
    return n;
}

/* 在 /etc/passwd 中按用户名查找, 找到则填入 out 并返回 1。字段指针指向
   内部静态缓冲, 本次查找的有效期内可安全使用。 */
static int get_user(const char *name, struct User *out) {
    static char buf[2048];
    int fd = (int) open("/etc/passwd", O_RDONLY);
    if (fd < 0) return 0;
    int len = 0;
    for (;;) {
        int n = (int) read(fd, buf + len, sizeof(buf) - 1 - len);
        if (n <= 0) break;
        len += n;
        if (len >= (int)sizeof(buf) - 1) break;
    }
    close(fd);
    buf[len] = 0;

    char *line = buf;
    while (*line) {
        char *nl = line;
        while (*nl && *nl != '\n') nl++;
        char saved = *nl;
        *nl = 0;
        if (line[0] && line[0] != '#') {
            char *f[8];
            int nf = split_colon(line, f, 8);
            if (nf >= 6 && str_eq(f[0], name)) {
                out->name = f[0];
                out->uid  = (usize) atoi(f[1]);
                out->gid  = (usize) atoi(f[2]);
                out->home = f[3];
                out->salt = f[4];
                out->hash = f[5];
                return 1;
            }
        }
        if (saved == 0) break;
        *nl = saved;
        line = nl + 1;
    }
    return 0;
}

static int str_eq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static int ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) return 0;
    return (int) mkdir(path);
}

/* 读一行(不回显)。返回字符数, 出错返回 -1。 */
static int readline(char *buf, int max) {
    int i = 0;
    for (;;) {
        int c = getchar();
        if (c < 0) return -1;
        if (c == '\n' || c == '\r') { buf[i] = 0; return i; }
        if (i < max - 1) buf[i++] = (char)c;
    }
}

/* 校验口令: SHA-256(salt ‖ pass) == u->hash */
static int check_pass(const struct User *u, const char *pass) {
    usize slen = 0; while (u->salt[slen]) slen++;
    usize plen = 0; while (pass[plen]) plen++;
    char blob[256];
    usize total = 0;
    for (usize i = 0; i < slen; i++) blob[total++] = u->salt[i];
    for (usize i = 0; i < plen; i++) blob[total++] = pass[i];
    unsigned char dig[32];
    char hex[65];
    sha256((const unsigned char *)blob, total, dig);
    to_hex(dig, hex);
    return str_eq(hex, u->hash);
}

int main(int argc, char *argv[]) {
    const char *target = "root";
    if (argc >= 2) target = argv[1];

    struct User u;
    if (!get_user(target, &u)) {
        puts("su: unknown user: ");
        puts(target);
        putchar('\n');
        return 1;
    }

    /* root 免密; 否则校验目标用户口令 */
    usize euid = (usize) geteuid();
    if (euid != 0) {
        char pass[64];
        puts("Password: ");
        if (readline(pass, sizeof(pass)) <= 0) return 1;
        if (!check_pass(&u, pass)) {
            puts("su: incorrect password\n");
            return 1;
        }
    }

    /* 确保目标家目录存在(非 root 可能无权限创建, 失败则忽略) */
    ensure_dir("/home");
    ensure_dir(u.home);

    isize pc = fork();
    if (pc == 0) {
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