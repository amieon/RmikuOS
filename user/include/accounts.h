#pragma once

#include "user.h"
#include "string.h"
#include "SHA256.h"

#define ACCT_DIR            "/var/etc"          /* 覆盖层目录 */
#define ACCT_PASSWD_STATE   "/var/etc/passwd"
#define ACCT_PASSWD_DEFAULT "/etc/passwd"
#define ACCT_GROUP_STATE    "/var/etc/group"
#define ACCT_GROUP_DEFAULT  "/etc/group"
#define ACCT_DB_MAX         4096               /* 库文件上限 */
#define ACCT_LINE_MAX       256                /* 单行上限 */

/* 小工具 */

/* 带上限的字符串拷贝（保证 '\0' 结尾） */
static void acct_copy(char *dst, const char *src, int cap) {
    int i = 0;
    if (cap <= 0) return;
    while (src[i] && i < cap - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* 把 s 追加到 buf[at..]，返回新的 at（始终保证结尾 '\0'） */
static int acct_append(char *buf, int at, int cap, const char *s) {
    if (at < 0) at = 0;
    for (int i = 0; s[i] && at < cap - 1; i++) buf[at++] = s[i];
    if (at < cap) buf[at] = 0;
    return at;
}

/* 十进制数追加到 buf[at..]，返回新的 at */
static int acct_put_uint(char *buf, int at, int cap, unsigned int v) {
    char tmp[16];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v > 0) { tmp[n++] = (char)('0' + (v % 10u)); v /= 10u; }
    for (int i = 0; i < n && at < cap - 1; i++) buf[at++] = tmp[n - 1 - i];
    if (at < cap) buf[at] = 0;
    return at;
}

/* 名字合法性：1..31 字节且不含 ':' '/' ',' '\n' */
static int acct_name_ok(const char *name) {
    if (!name || !name[0]) return 0;
    for (int i = 0; name[i]; i++) {
        char c = name[i];
        if (c == ':' || c == '/' || c == ',' || c == '\n') return 0;
        if (i >= 31) return 0;
    }
    return 1;
}

/* 库读写 */

/* 打开：覆盖层优先，回退出厂默认 */
static int acct_open(const char *state, const char *dflt) {
    int fd = (int) open(state, O_RDONLY);
    if (fd >= 0) return fd;
    return (int) open(dflt, O_RDONLY);
}

/* 读整个库到 buf，返回字节数；失败返回 -1 */
static int acct_load(char *buf, int cap, const char *state, const char *dflt) {
    int fd = acct_open(state, dflt);
    if (fd < 0) return -1;
    int len = 0;
    for (;;) {
        if (len >= cap - 1) break;
        int n = (int) read(fd, buf + len, (usize)(cap - 1 - len));
        if (n <= 0) break;
        len += n;
    }
    close(fd);
    buf[len] = 0;
    return len;
}

/* 写覆盖层：确保目录存在 + 覆盖写 + fsync。成功返回 0 */
static int acct_store(const char *state, const char *buf, int len) {
    mkdir(ACCT_DIR, 0777);        /* 已存在时报错，忽略即可 */
    int fd = (int) open(state, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    int off = 0;
    while (off < len) {
        isize n = write(fd, buf + off, (usize)(len - off));
        if (n <= 0) break;
        off += (int) n;
    }
    fsync(fd);                    /* 账户变更属于关键状态，立刻落盘 */
    close(fd);
    return (off == len) ? 0 : -1;
}

/* 解析 */

/* 逐行迭代：把 buf[off..] 的一行复制进 line（不含换行），返回下一行起始偏移；
 * 迭代结束返回 -1。不修改 buf。 */
static int acct_next_line(const char *buf, int off, char *line, int cap) {
    if (off < 0 || buf[off] == 0) return -1;
    int i = 0;
    while (buf[off] && buf[off] != '\n') {
        if (i < cap - 1) line[i++] = buf[off];
        off++;
    }
    line[i] = 0;
    if (buf[off] == '\n') off++;
    return off;
}

/* 把 line 按 ':' 拆成字段（line 会被改写），返回字段数 */
static int acct_fields(char *line, char *f[], int max) {
    int n = 1;
    f[0] = line;
    for (char *p = line; *p && n < max; p++) {
        if (*p == ':') { *p = 0; f[n++] = p + 1; }
    }
    return n;
}

/* 逗号分隔的 members 里是否含 user（精确匹配） */
static int acct_member_has(const char *members, const char *user) {
    const char *p = members;
    while (*p) {
        const char *q = p;
        while (*q && *q != ',') q++;
        int n = (int)(q - p);
        int hit = (n > 0);
        if (hit) {
            if ((int)strlen(user) != n) hit = 0;
            else for (int i = 0; i < n; i++) if (p[i] != user[i]) { hit = 0; break; }
        }
        if (hit) return 1;
        if (*q == 0) break;
        p = q + 1;
    }
    return 0;
}

/* 追加一整行到库缓冲（自动补换行），返回新长度；空间不足返回 -1 */
static int acct_append_line(char *db, int len, int cap, const char *line) {
    int n = 0;
    while (line[n]) n++;
    if (len + n + 2 > cap) return -1;
    if (len > 0 && db[len - 1] != '\n') db[len++] = '\n';
    for (int i = 0; i < n; i++) db[len++] = line[i];
    db[len++] = '\n';
    db[len] = 0;
    return len;
}

/* 口令 */

/* SHA-256(salt ‖ 口令) 的十六进制 */
static void acct_hash(const char *salt, const char *pass, char hex[65]) {
    char blob[256];
    int total = 0;
    for (int i = 0; salt[i] && total < 250; i++) blob[total++] = salt[i];
    for (int i = 0; pass[i] && total < 250; i++) blob[total++] = pass[i];
    unsigned char dig[32];
    sha256((const unsigned char *)blob, (usize) total, dig);
    to_hex(dig, hex);
}

/* 生成盐: s4lt_<名前4字符>_<pid>。
 * 注: 真实系统应使用 CSPRNG；教学 OS 用可复现的伪随机足够。 */
static int acct_salt(char *out, const char *name) {
    int at = 0;
    at = acct_append(out, at, 32, "s4lt_");
    for (int i = 0; name[i] && i < 4 && at < 30; i++) out[at++] = name[i];
    out[at] = 0;
    at = acct_append(out, at, 32, "_");
    at = acct_put_uint(out, at, 32, (unsigned int) getpid());
    return at;
}

/* 读一行口令（回显；与当前终端 raw mode 支持一致，关回显是后续改进项） */
static int acct_readline(char *buf, int max) {
    int i = 0;
    for (;;) {
        int c = getchar();
        if (c < 0) return -1;
        if (c == '\n' || c == '\r') { buf[i] = 0; return i; }
        if (i < max - 1) buf[i++] = (char) c;
    }
}
