#include "sys.h"
#define FILE_TYPE_FILE 1
#define FILE_TYPE_DIR  2

struct dirent {
    unsigned char file_type;
    unsigned char name_len;
    unsigned char reserved[6];
    char name[56];
};

static inline isize getdents(int fd, struct dirent *buf, usize len) {
    return syscall3(SYS_GETDENTS, (usize)fd, (usize)buf, len);
}

#define SYS_EXEC 7

#define EXEC_MAX_ARGS 8

struct user_arg {
    const char *ptr;
    usize len;
};

struct exec_args {
    usize argc;
    struct user_arg argv[EXEC_MAX_ARGS];
};

static inline isize exec2(const char *name, usize len) {
    return syscall3(SYS_EXEC, (usize)name, len, 0);
}

static inline isize exec_with_args(const char *path, struct exec_args *args) {
    return syscall3(SYS_EXEC, (usize)path, strlen(path), (usize)args);
}

static inline isize exec(const char *path) {
    struct exec_args args;
    args.argc = 1;
    args.argv[0].ptr = path;
    args.argv[0].len = strlen(path);

    for (int i = 1; i < EXEC_MAX_ARGS; i++) {
        args.argv[i].ptr = 0;
        args.argv[i].len = 0;
    }

    return exec_with_args(path, &args);
}

static inline isize chdir2(const char *path, usize len) {
    return syscall3(SYS_CHDIR, (usize)path, len, 0);
}

static inline isize chdir(const char *path) {
    return chdir2(path, strlen(path));
}

static inline isize getcwd(char *buf, usize len) {
    return syscall3(SYS_GETCWD, (usize)buf, len, 0);
}