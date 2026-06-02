#include "user.h"

#define DEFAULT_LOOPS 200
#define MAX_PATH 128
#define DIRENT_BATCH 3

static int failures = 0;
static int verbose = 0;

static int streq(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == 0 && b[i] == 0;
}

static int parse_positive_int(const char *s, int fallback) {
    int x = 0;
    int i = 0;

    if (!s || !s[0]) {
        return fallback;
    }

    while (s[i]) {
        if (s[i] < '0' || s[i] > '9') {
            return fallback;
        }
        x = x * 10 + (s[i] - '0');
        i++;
    }

    if (x <= 0) {
        return fallback;
    }

    return x;
}

static void fail_msg(const char *where, const char *detail) {
    failures++;

    puts("[FAIL] ");
    puts(where);

    if (detail) {
        puts(": ");
        puts(detail);
    }

    puts("\n");
}

static void ok_msg(const char *where) {
    if (!verbose) return;

    puts("[ OK ] ");
    puts(where);
    puts("\n");
}

static void print_type(unsigned char type) {
    if (type == STAT_TYPE_DIR || type == FILE_TYPE_DIR) {
        put_char('d');
    } else if (type == STAT_TYPE_FILE || type == FILE_TYPE_FILE) {
        put_char('-');
    } else {
        put_char('?');
    }
}

static int valid_dirent_name(struct dirent *d) {
    if (d->name_len == 0) return 0;
    if (d->name_len > 56) return 0;

    for (int i = 0; i < d->name_len; i++) {
        unsigned char c = (unsigned char)d->name[i];
        if (c == 0) return 0;
        if (c == '/') return 0;
    }

    return 1;
}

static int check_stat_path(const char *path, unsigned char expected_type) {
    struct stat st;

    if (stat(path, &st) < 0) {
        fail_msg("stat failed", path);
        return -1;
    }

    if (expected_type != 0 && st.file_type != expected_type) {
        puts("[FAIL] stat type mismatch: ");
        puts(path);
        puts(" got=");
        put_int(st.file_type);
        puts(" expected=");
        put_int(expected_type);
        puts("\n");
        failures++;
        return -1;
    }

    if (verbose) {
        puts("[stat] ");
        print_type(st.file_type);
        puts(" size=");
        put_int(st.size);
        puts(" ");
        puts(path);
        puts("\n");
    }

    return 0;
}

static int check_fstat_fd(int fd, unsigned char expected_type, const char *tag) {
    struct stat st;

    if (fstat(fd, &st) < 0) {
        fail_msg("fstat failed", tag);
        return -1;
    }

    if (expected_type != 0 && st.file_type != expected_type) {
        puts("[FAIL] fstat type mismatch: ");
        puts(tag);
        puts(" got=");
        put_int(st.file_type);
        puts(" expected=");
        put_int(expected_type);
        puts("\n");
        failures++;
        return -1;
    }

    return 0;
}

static int scan_dir(const char *path, int min_entries) {
    int fd = open(path);

    if (fd < 0) {
        fail_msg("open dir failed", path);
        return -1;
    }

    check_fstat_fd(fd, STAT_TYPE_DIR, path);

    struct dirent entries[DIRENT_BATCH];
    int total = 0;

    while (1) {
        isize n = getdents(fd, entries, sizeof(entries));

        if (n < 0) {
            fail_msg("getdents failed", path);
            close(fd);
            return -1;
        }

        if (n == 0) {
            break;
        }

        if (n % sizeof(struct dirent) != 0) {
            fail_msg("getdents returned unaligned size", path);
            close(fd);
            return -1;
        }

        int count = n / sizeof(struct dirent);

        for (int i = 0; i < count; i++) {
            struct dirent *d = &entries[i];

            if (!valid_dirent_name(d)) {
                fail_msg("bad dirent name", path);
                close(fd);
                return -1;
            }

            if (d->file_type != FILE_TYPE_FILE && d->file_type != FILE_TYPE_DIR) {
                fail_msg("bad dirent type", path);
                close(fd);
                return -1;
            }

            if (verbose) {
                puts("[dirent] ");
                print_type(d->file_type);
                put_char(' ');
                write(1, d->name, d->name_len);
                puts("\n");
            }

            total++;
        }
    }

    close(fd);

    if (total < min_entries) {
        puts("[FAIL] too few entries in ");
        puts(path);
        puts(": ");
        put_int(total);
        puts("\n");
        failures++;
        return -1;
    }

    return total;
}

static int read_all_file(const char *path, int min_bytes) {
    int fd = open(path);

    if (fd < 0) {
        fail_msg("open file failed", path);
        return -1;
    }

    struct stat st;

    if (fstat(fd, &st) < 0) {
        fail_msg("fstat file failed", path);
        close(fd);
        return -1;
    }

    if (st.file_type != STAT_TYPE_FILE) {
        fail_msg("opened path is not file", path);
        close(fd);
        return -1;
    }

    /*
     * 故意用奇怪大小的 buffer，测跨块/跨页读。
     */
    char buf[37];
    int total = 0;

    while (1) {
        isize n = read(fd, buf, sizeof(buf));

        if (n < 0) {
            fail_msg("read file failed", path);
            close(fd);
            return -1;
        }

        if (n == 0) {
            break;
        }

        total += n;
    }

    close(fd);

    if (total < min_bytes) {
        puts("[FAIL] file too small or read incomplete: ");
        puts(path);
        puts(" total=");
        put_int(total);
        puts("\n");
        failures++;
        return -1;
    }

    if (st.size != 0 && total != (int)st.size) {
        puts("[FAIL] read size != stat size: ");
        puts(path);
        puts(" read=");
        put_int(total);
        puts(" stat=");
        put_int(st.size);
        puts("\n");
        failures++;
        return -1;
    }

    return total;
}

static int fd_reuse_test(void) {
    int fds[16];

    for (int i = 0; i < 16; i++) {
        fds[i] = open("/etc/motd");

        if (fds[i] < 0) {
            fail_msg("fd_reuse open failed", "/etc/motd");
            return -1;
        }

        check_fstat_fd(fds[i], STAT_TYPE_FILE, "fd_reuse");
    }

    /*
     * 倒序 close，这样如果 free_fds 是栈，下一次 open 应该复用最小的 fd=3。
     */
    for (int i = 15; i >= 0; i--) {
        if (close(fds[i]) < 0) {
            fail_msg("fd_reuse close failed", 0);
            return -1;
        }
    }

    int fd = open("/etc/motd");

    if (fd < 0) {
        fail_msg("fd_reuse reopen failed", "/etc/motd");
        return -1;
    }

    if (fd != 3) {
        puts("[WARN] fd reuse did not return 3, got ");
        put_int(fd);
        puts("\n");
        /*
         * 这里先不算失败，因为你的 free_fds 策略可能不是严格最小 fd。
         * 但如果 fd 一直涨，这里会很明显。
         */
    }

    close(fd);

    return 0;
}

static int cwd_relative_test(void) {
    char old_cwd[MAX_PATH];

    if (getcwd(old_cwd, sizeof(old_cwd)) < 0) {
        fail_msg("getcwd failed before cwd test", 0);
        return -1;
    }

    if (chdir("/") < 0) {
        fail_msg("chdir / failed", 0);
        return -1;
    }

    char cwd[MAX_PATH];

    if (getcwd(cwd, sizeof(cwd)) < 0 || !streq(cwd, "/")) {
        fail_msg("cwd should be /", cwd);
        chdir(old_cwd);
        return -1;
    }

    if (chdir("bin") < 0) {
        fail_msg("chdir relative bin failed", 0);
        chdir(old_cwd);
        return -1;
    }

    if (getcwd(cwd, sizeof(cwd)) < 0 || !streq(cwd, "/bin")) {
        fail_msg("cwd should be /bin", cwd);
        chdir(old_cwd);
        return -1;
    }

    if (check_stat_path("hello", STAT_TYPE_FILE) < 0) {
        chdir(old_cwd);
        return -1;
    }

    if (read_all_file("hello", 1) < 0) {
        chdir(old_cwd);
        return -1;
    }

    if (chdir("../etc") < 0) {
        fail_msg("chdir ../etc failed", 0);
        chdir(old_cwd);
        return -1;
    }

    if (getcwd(cwd, sizeof(cwd)) < 0 || !streq(cwd, "/etc")) {
        fail_msg("cwd should be /etc", cwd);
        chdir(old_cwd);
        return -1;
    }

    if (check_stat_path("motd", STAT_TYPE_FILE) < 0) {
        chdir(old_cwd);
        return -1;
    }

    if (read_all_file("motd", 1) < 0) {
        chdir(old_cwd);
        return -1;
    }

    if (chdir("..") < 0) {
        fail_msg("chdir .. failed", 0);
        chdir(old_cwd);
        return -1;
    }

    if (getcwd(cwd, sizeof(cwd)) < 0 || !streq(cwd, "/")) {
        fail_msg("cwd should return to /", cwd);
        chdir(old_cwd);
        return -1;
    }

    if (chdir(old_cwd) < 0) {
        fail_msg("restore old cwd failed", old_cwd);
        return -1;
    }

    return 0;
}

static void one_round(int round) {
    if (verbose) {
        puts("\n--- round ");
        put_int(round);
        puts(" ---\n");
    }

    check_stat_path("/", STAT_TYPE_DIR);
    check_stat_path("/bin", STAT_TYPE_DIR);
    check_stat_path("/etc", STAT_TYPE_DIR);
    check_stat_path("/etc/motd", STAT_TYPE_FILE);
    check_stat_path("/bin/hello", STAT_TYPE_FILE);

    scan_dir("/", 2);
    scan_dir("/bin", 1);
    scan_dir("/etc", 1);

    read_all_file("/etc/motd", 1);
    read_all_file("/bin/hello", 1);

    fd_reuse_test();
    cwd_relative_test();
}

int main(int argc, char **argv) {
    int loops = DEFAULT_LOOPS;

    if (argc >= 2) {
        loops = parse_positive_int(argv[1], DEFAULT_LOOPS);
    }

    if (argc >= 3 && streq(argv[2], "-v")) {
        verbose = 1;
    }

    puts("\n========== RmikuOS FS Stress ==========\n");
    puts("pid=");
    put_int(getpid());
    puts(" loops=");
    put_int(loops);
    puts("\n");

    char cwd[MAX_PATH];
    if (getcwd(cwd, sizeof(cwd)) >= 0) {
        puts("start cwd=");
        puts(cwd);
        puts("\n");
    }

    for (int i = 1; i <= loops; i++) {
        one_round(i);

        if (i % 20 == 0) {
            puts("[progress] ");
            put_int(i);
            puts("/");
            put_int(loops);
            puts(" failures=");
            put_int(failures);
            puts("\n");
        }


        if (failures >= 20) {
            puts("[abort] too many failures\n");
            break;
        }
    }

    puts("\n========== FS Stress Summary ==========\n");
    puts("loops=");
    put_int(loops);
    puts(" failures=");
    put_int(failures);
    puts("\n");

    if (failures == 0) {
        puts("FS STRESS PASS\n");
        return 0;
    } else {
        puts("FS STRESS FAIL\n");
        return 1;
    }
}