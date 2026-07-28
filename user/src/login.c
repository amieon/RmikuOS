#include "user.h"
#include "SHA256.h"


struct User {
    const char *name;
    usize uid, gid;
    const char *home;
    const char *salt;   /* 每用户一个盐, 与口令拼接后再哈希 */
    const char *hash;   /* SHA-256(salt ‖ 口令) 的十六进制 */
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

/* 若目录不存在则创建(已存在时 mkdir 报错被忽略)。返回 0 表示最终存在。 */
static int ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) return 0;   /* 已存在 */
    return (int) mkdir(path);             /* 不存在则尝试创建 */
}

/* 读一行(到 \n/\r 为止), 不回显。返回字符数, 出错返回 -1。 */
static int readline(char *buf, int max) {
    int i = 0;
    for (;;) {
        int c = getchar();
        if (c < 0) return -1;
        if (c == '\n' || c == '\r') { buf[i] = 0; return i; }
        if (i < max - 1) buf[i++] = (char)c;
    }
}

int main(void) {
    char name[64], pass[64], hex[65];
    unsigned char dig[32];

    for (;;) {
        puts("RmikuOS login: ");
        if (readline(name, sizeof(name)) <= 0) continue;
        puts("Password: ");
        if (readline(pass, sizeof(pass)) <= 0) continue;

        struct User u;
        if (!get_user(name, &u)) { puts("login: unknown user\n"); continue; }

        /* 校验: SHA-256(salt ‖ 口令) 与文件中哈希比对 */
        usize slen = 0; while (u.salt[slen]) slen++;
        usize plen = 0; while (pass[plen]) plen++;
        char blob[256];
        usize total = 0;
        for (usize i = 0; i < slen; i++) blob[total++] = u.salt[i];
        for (usize i = 0; i < plen; i++) blob[total++] = pass[i];
        sha256((const unsigned char *)blob, total, dig);
        to_hex(dig, hex);
        if (!str_eq(hex, u.hash)) { puts("login: incorrect password\n"); continue; }

        /* 建家目录并归属该用户(此时仍是 root, 可 chown) */
        ensure_dir("/home");
        ensure_dir(u.home);
        chown(u.home, u.uid, u.gid);

        /* 认证成功: 子进程降权后 exec shell; 父进程回到登录循环 */
        isize pc = fork();
        if (pc == 0) {
            setgid(u.gid);           /* 必须先 setgid(此时仍是 root) */
            setuid(u.uid);           /* 再 setuid, euid 降为非 0 */
            chdir(u.home);           /* 家目录(不存在则静默留在 /) */
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