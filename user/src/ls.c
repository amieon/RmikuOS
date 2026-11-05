#include "user.h"
#include <time.h>      /* gmtime_r —— 文件 mtime 格式化 */
#include <pwd.h>       /* getpwuid —— 整份 inline 在 pwd.h 里, 直接 include 就行 */

static void copy_dirent_name(struct dirent *d, char *out, int out_size) {
    int n = d->name_len;
    if (n > out_size - 1) {
        n = out_size - 1;
    }

    for (int i = 0; i < n; i++) {
        out[i] = d->name[i];
    }

    out[n] = 0;
}

static void join_path(const char *dir, const char *name, char *out, int out_size) {
    int pos = 0;

    if (dir[0] == '.' && dir[1] == 0) {
        for (int i = 0; name[i] && pos < out_size - 1; i++) {
            out[pos++] = name[i];
        }
        out[pos] = 0;
        return;
    }

    for (int i = 0; dir[i] && pos < out_size - 1; i++) {
        out[pos++] = dir[i];
    }

    if (pos > 0 && out[pos - 1] != '/' && pos < out_size - 1) {
        out[pos++] = '/';
    }

    for (int i = 0; name[i] && pos < out_size - 1; i++) {
        out[pos++] = name[i];
    }

    out[pos] = 0;
}

/* st_mode -> "-rwxrwxrwx" 风格(类型位 + 权限位)。
 * 权限位即内核 mode 的低 12 位(0400=user-r ... 0001=other-x)。 */
static void print_mode(mode_t m) {
    char t;
    if (S_ISDIR(m))        t = 'd';
    else if (S_ISLNK(m))   t = 'l';
    else if (S_ISCHR(m))   t = 'c';
    else if (S_ISFIFO(m))  t = 'p';
    else                   t = '-';
    printf("%c%c%c%c%c%c%c%c%c%c",
           t,
           (m & 0400) ? 'r' : '-', (m & 0200) ? 'w' : '-', (m & 0100) ? 'x' : '-',
           (m & 0040) ? 'r' : '-', (m & 0020) ? 'w' : '-', (m & 0010) ? 'x' : '-',
           (m & 0004) ? 'r' : '-', (m & 0002) ? 'w' : '-', (m & 0001) ? 'x' : '-');
}

/* 属主名: 账户库里查得到就显示名字, 查不到回退 uid 数字 */
static void print_owner(uid_t uid) {
    struct passwd *pw = getpwuid(uid);
    if (pw && pw->pw_name) {
        printf("%-8s", pw->pw_name);
    } else {
        printf("%-8u", uid);
    }
}

/* 长格式一行: 类型权限 属主 大小 时间 名字 */
static void print_long(const char *name, const char *full_path, struct stat *st) {
    struct tm tm;

    print_mode(st->st_mode);
    fputs(" ", stdout);
    print_owner(st->st_uid);
    printf(" %8lu ", (unsigned long)st->st_size);

    if (gmtime_r(&st->st_mtime, &tm)) {
        printf("%04d-%02d-%02d %02d:%02d ",
               tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
               tm.tm_hour, tm.tm_min);
    } else {
        fputs("?????????? ", stdout);
    }

    fputs(name, stdout);
    if (S_ISDIR(st->st_mode)) {
        fputs("/", stdout);
    } else if (S_ISLNK(st->st_mode)) {
        /* symlink 长格式显示 "name -> target", 与 Linux ls -l 一致 */
        char target[128];
        isize n = readlink(full_path, target, sizeof(target) - 1);
        if (n >= 0) {
            target[n] = 0;
            fputs(" -> ", stdout);
            fputs(target, stdout);
        }
    }
    fputs("\n", stdout);
}

/* 短格式一行: 类型 大小 名字 */
static void print_short(const char *name, const struct stat *st) {
    switch (stat_type_of(st->st_mode)) {
        case STAT_TYPE_DIR:     fputs("dir     ", stdout); break;
        case STAT_TYPE_FILE:    fputs("file    ", stdout); break;
        case STAT_TYPE_SYMLINK: fputs("link    ", stdout); break;
        case STAT_TYPE_CHAR:    fputs("char    ", stdout); break;
        default:                fputs("unknown ", stdout); break;
    }
    printf("%lu ", (unsigned long)st->st_size);
    fputs(name, stdout);
    if (S_ISDIR(st->st_mode)) {
        fputs("/", stdout);
    }
    fputs("\n", stdout);
}

/* 处理一个操作数: 非目录打印该条目自身, 目录则列 children。 */
static int list_one(const char *path, int long_fmt, int show_header) {
    struct stat self;

    if (lstat(path, &self) < 0) {
        fputs("ls: cannot stat ", stdout);
        fputs(path, stdout);
        fputs(": ", stdout);
        fputs(strerror(errno), stdout);
        fputs("\n", stdout);
        fflush(stdout);
        return -1;
    }

    if (!S_ISDIR(self.st_mode)) {
        if (long_fmt) print_long(path, path, &self);
        else print_short(path, &self);
        fflush(stdout);
        return 0;
    }

    if (show_header) {
        fputs(path, stdout);
        fputs(":\n", stdout);
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fputs("ls: cannot open ", stdout);
        fputs(path, stdout);
        fputs(": ", stdout);
        fputs(strerror(errno), stdout);
        fputs("\n", stdout);
        fflush(stdout);
        return -1;
    }

    struct dirent entries[8];
    int rc = 0;

    while (1) {
        isize n = getdents(fd, entries, sizeof(entries));

        if (n < 0) {
            fputs("ls: read directory failed: ", stdout);
            fputs(path, stdout);
            fputs(": ", stdout);
            fputs(strerror(errno), stdout);
            fputs("\n", stdout);
            rc = -1;
            break;
        }

        if (n == 0) {
            break;
        }

        int count = n / sizeof(struct dirent);

        for (int i = 0; i < count; i++) {
            char name[64];
            char full_path[128];
            struct stat st;

            copy_dirent_name(&entries[i], name, sizeof(name));
            join_path(path, name, full_path, sizeof(full_path));

            /* 用 lstat 而不是 stat: 目录里的 symlink 要显示链接本身,
             * 而不是跟随到目标( dangling 链接的目标甚至可能不存在 )。 */
            if (lstat(full_path, &st) < 0) {
                fputs("?       ", stdout);
                fputs(name, stdout);
                fputs("\n", stdout);
                continue;
            }

            if (long_fmt) print_long(name, full_path, &st);
            else print_short(name, &st);
        }

        fflush(stdout);
    }

    close(fd);
    return rc;
}

#define MAX_LS_PATHS 32

int main(int argc, char *argv[]) {
    int long_fmt = 0;
    const char *paths[MAX_LS_PATHS];
    int npaths = 0;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            /* 目前只认 -l; getdents 不返回 . / .., 所以 -a 天然等价 */
            if (argv[i][1] == 'l') long_fmt = 1;
        } else if (npaths < MAX_LS_PATHS) {
            paths[npaths++] = argv[i];
        }
    }

    if (npaths == 0) paths[npaths++] = ".";

    int status = 0;
    for (int i = 0; i < npaths; i++) {
        if (list_one(paths[i], long_fmt, npaths > 1) < 0) status = 1;
    }
    return status;
}
