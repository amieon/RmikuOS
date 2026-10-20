#pragma once
#ifdef __cplusplus
extern "C" {
#endif


#include "io.h"
#include "sys/types.h"   /* mode_t / uid_t / gid_t / time_t / off_t */
#include "errno.h"       /* access() 失败时按 amode 粗设 errno */
#include "process.h"

/* 
 * 本文件是用户态唯一的 struct stat / struct dirent 定义点（方案 A 统一）。
 *
 *   - struct stat   是 POSIX 布局（st_mode 等），stat()/fstat() 内部
 *                   用私有 32 字节内核布局接收 syscall 结果再翻译。
 *   - struct dirent 保持内核 getdents 字节布局（64 字节/条, 内核契约），
 *                   但加了 d_name 别名, 让 POSIX 代码也能直接读名字。
 */

/* ---- 目录项 ---- */

#define FILE_TYPE_FILE 1
#define FILE_TYPE_DIR  2
#define FILE_TYPE_SYMLINK 3

/* 
 * at 家族的 flag
 *
 * dirfd 的语义（与 POSIX 一致）：
 *   - 已打开的目录 fd → path 相对它解析
 *   - AT_FDCWD        → 相对进程 cwd
 *   - path 是绝对路径 → dirfd 被完全忽略（哪怕是 -1 这种无效值）
 * AT_EACCESS 与 AT_REMOVEDIR 数值相同是**故意的**：POSIX 里它们分属
 * faccessat 与 unlinkat，不会同时出现在一个调用里。
 */

#define AT_FDCWD            (-100)
#define AT_SYMLINK_NOFOLLOW 0x100   /* fstatat/fchmodat: 不跟随最终 symlink */
#define AT_REMOVEDIR        0x200   /* unlinkat: 删目录（等价 rmdir） */
#define AT_EACCESS          0x200   /* faccessat: 用有效身份(euid)而非真实身份(ruid) */
#define AT_SYMLINK_FOLLOW   0x400   /* linkat: 跟随源 symlink（未实现，返回 -1） */

/*
 * 内核 getdents 的字节布局：64 字节/条，必须与 kernel/src/fs/dirent.rs 一致。
 *
 * 关于 d_type：POSIX 要求 dirent 有 d_type，但这块 ABI 没留空位，
 * 于是借 reserved[0]（内核只写 file_type/name_len/name，reserved 是
 * don't care，从不回读；偏移和总长都没变）。两者语义不同：
 *   file_type = 内核枚举（1=文件 2=目录 3=符号链接）
 *   d_type    = POSIX DT_* 常量（4=目录 8=普通文件 10=符号链接 …）
 * readdir() 在拷进 POSIX 视图时把前者翻译成后者。
 */
struct dirent {
    unsigned char file_type;   /* 1=文件 2=目录（内核值） */
    unsigned char name_len;    /* 名字长度, name 不带 NUL */
    unsigned char d_type;      /* @2: DT_*（POSIX 值, 只有 readdir 填） */
    unsigned char reserved[5];
    union {
        char name[56];         /* 内核字段名 */
        char d_name[56];       /* POSIX 别名（布局相同） */
    };
};

static inline isize getdents(int fd, struct dirent *buf, usize len) {
    return syscall3(SYS_GETDENTS, (usize)fd, (usize)buf, len);
}

/* ---- 工作目录 ---- */

static inline isize chdir2(const char *path, usize len) {
    return syscall3(SYS_CHDIR, (usize)path, len, 0);
}

static inline int chdir(const char *path) {
    return (int)chdir2(path, strlen(path));
}

/* fchdir(fd): 切到已打开的目录 fd。
 * 比 chdir("路径") 安全：路径在"解析完到切换"之间可能被别的线程改掉，
 * fd 指向的已经是那个目录本身。非目录 fd / 目录已被删除 → -1。 */
static inline int fchdir(int fd) {
    return (int)syscall3(SYS_FCHDIR, (usize)fd, 0, 0);
}

/* POSIX 语义: 成功返回 buf, 失败返回 NULL */
static inline char *getcwd(char *buf, usize len) {
    if (syscall3(SYS_GETCWD, (usize)buf, len, 0) < 0) return (char *)0;
    return buf;
}

/* ---- 文件元信息 ---- */

#define STAT_TYPE_FILE 1
#define STAT_TYPE_DIR  2
#define STAT_TYPE_CHAR 3
#define STAT_TYPE_PIPE 4
#define STAT_TYPE_SYMLINK 6

/* ---- 文件类型位（POSIX, 并入 st_mode 高 4 位） ---- */
#define S_IFMT   0170000
#define S_IFSOCK 0140000
#define S_IFLNK  0120000
#define S_IFREG  0100000
#define S_IFBLK  0060000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFIFO  0010000

#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

/* ---- 权限位(低 12 位, 与内核 S_* 常量一致)。注意: C 八进制用前导 0, 不是 0o。 ---- */
#define S_IRWXU 0700
#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100
#define S_IRWXG 0070
#define S_IRGRP 0040
#define S_IWGRP 0020
#define S_IXGRP 0010
#define S_IRWXO 0007
#define S_IROTH 0004
#define S_IWOTH 0002
#define S_IXOTH 0001
#define S_ISUID 04000
#define S_ISGID 02000
#define S_ISVTX 01000

/*
 * POSIX struct stat —— 用户态唯一结构。
 *
 * 重要：不能把它直接当 syscall 缓冲用！内核 Stat 是 32 字节原始布局，
 * 由下面的 stat()/fstat() 翻译成此结构（类型位并入 st_mode；mtime 在墙钟
 * 校准后为真值, 未校准=0）。
 * 内核布局见 rk_stat_kernel_t（与 kernel/src/fs/stat.rs 的 Stat 完全一致）。
 */
struct stat {
    mode_t   st_mode;       /* 权限位 | S_IF* 类型位 */
    usize    st_size;       /* 文件字节数 */
    uid_t    st_uid;        /* 属主 */
    gid_t    st_gid;        /* 属组 */
    time_t   st_mtime;      /* 最后修改时间(epoch 秒, 墙钟校准后为真值) */
    time_t   st_atime;      /* 教学简化: 同 mtime */
    time_t   st_ctime;      /* 教学简化: 同 mtime */
    long     st_blksize;    /* 近似值 512 */
    long     st_blocks;     /* 恒 0 */

    /* 以下三个字段本内核没有真实语义, 但必须存在 —— POSIX 程序会直接读它们*/
    dev_t    st_dev;        /* 恒 0(无设备号概念) */
    ino_t    st_ino;        /* 恒 0(无 inode 号) */
    nlink_t  st_nlink;      /* 目录 2 / 其余 1(假装没有子目录, du/find 够用) */
    dev_t    st_rdev;       /* 恒 0(无设备文件) */
};

/* 内核 Stat 的私有字节布局（32 字节, 必须与 kernel/src/fs/stat.rs 一致） */
typedef struct rk_stat_kernel {
    unsigned char file_type;   /* @0 */
    unsigned short mode;       /* @2 */
    unsigned int   uid;        /* @4 */
    unsigned int   gid;        /* @8 */
    usize          size;       /* @16 */
    unsigned int   mtime;      /* @24 最后修改时间(epoch 秒, 墙钟校准后为真值) */
} rk_stat_kernel_t;

static inline int rk_stat_convert(const rk_stat_kernel_t *k, struct stat *st) {
    if (!st) return -1;
    st->st_size = (usize)k->size;
    st->st_uid  = (uid_t)k->uid;
    st->st_gid  = (gid_t)k->gid;
    st->st_mode = (mode_t)k->mode;
    /* 合并类型位: 内核 mode 只有权限位, 不合并则 S_ISDIR() 永远假 */
    switch (k->file_type) {
        case STAT_TYPE_DIR:  st->st_mode |= S_IFDIR; break;
        case STAT_TYPE_CHAR: st->st_mode |= S_IFCHR; break;
        case STAT_TYPE_PIPE: st->st_mode |= S_IFIFO; break;
        case STAT_TYPE_SYMLINK: st->st_mode |= S_IFLNK; break;
        default:             st->st_mode |= S_IFREG; break;
    }
    /* 墙钟校准后 mtime 为真值(epoch 秒); 未校准=0。
     * atime/ctime 教学简化: 与 mtime 相同。 */
    st->st_mtime = (time_t)k->mtime;
    st->st_atime = (time_t)k->mtime;
    st->st_ctime = (time_t)k->mtime;
    st->st_blksize = 512; st->st_blocks = 0;

    /* 内核没有设备号 / inode 号 / 硬链接计数, 这里填常量占位 —— 字段的存在性
     * 比取值重要(见 struct stat 里的说明)。目录给 2 是 POSIX 惯例(自身 + "."),
     * 我们不计子目录, 对 du/find 这类"判断是否还有别的链接"的用法足够。 */
    st->st_dev   = 0;
    st->st_ino   = 0;
    st->st_rdev  = 0;
    st->st_nlink = ((st->st_mode & S_IFMT) == S_IFDIR) ? 2 : 1;
    return 0;
}

/* 把 st_mode 还原为 STAT_TYPE_* 码（1/2/3/4）。
 * 兼容旧代码里直接读 st.file_type 的写法:
 *   st.file_type == STAT_TYPE_DIR  →  stat_type_of(st.st_mode) == STAT_TYPE_DIR */
static inline int stat_type_of(mode_t m) {
    if (S_ISDIR(m))  return STAT_TYPE_DIR;
    if (S_ISCHR(m))  return STAT_TYPE_CHAR;
    if (S_ISFIFO(m)) return STAT_TYPE_PIPE;
    if (S_ISLNK(m))  return STAT_TYPE_SYMLINK;
    return STAT_TYPE_FILE;
}

static inline isize stat2(const char *path, usize len, struct stat *st) {
    rk_stat_kernel_t k;
    isize r = syscall3(SYS_STAT, (usize)path, len, (usize)&k);
    if (r < 0) return r;
    return (isize)rk_stat_convert(&k, st);
}

static inline int stat(const char *path, struct stat *st) {
    return (int)stat2(path, strlen(path), st);
}

/* lstat: 路径中间 symlink 仍跟随，但最终 symlink 返回链接自身 */
static inline isize lstat2(const char *path, usize len, struct stat *st) {
    rk_stat_kernel_t k;
    isize r = syscall3(SYS_LSTAT, (usize)path, len, (usize)&k);
    if (r < 0) return r;
    return (isize)rk_stat_convert(&k, st);
}

static inline int lstat(const char *path, struct stat *st) {
    return (int)lstat2(path, strlen(path), st);
}

static inline int fstat(int fd, struct stat *st) {
    rk_stat_kernel_t k;
    isize r = syscall3(SYS_FSTAT, (usize)fd, (usize)&k, 0);
    if (r < 0) return -1;
    return rk_stat_convert(&k, st);
}

/* fstatat(dirfd, path, st, flags): 相对目录 fd 取属性。
 * flags 带 AT_SYMLINK_NOFOLLOW → 等价 lstat（看链接本身）。 */
static inline isize fstatat2(int dirfd, const char *path, usize len,
                             struct stat *st, usize flags) {
    rk_stat_kernel_t k;
    isize r = syscall6(SYS_FSTATAT, (usize)(isize)dirfd, (usize)path, len,
                       (usize)&k, flags, 0);
    if (r < 0) return r;
    return (isize)rk_stat_convert(&k, st);
}

static inline int fstatat(int dirfd, const char *path, struct stat *st, int flags) {
    return (int)fstatat2(dirfd, path, strlen(path), st, (usize)flags);
}

/* chmod(path, mode): 修改文件权限位(低 12 位)。仅 root 或文件属主可改。 */
static inline isize chmod2(const char *path, usize len, usize mode) {
    return syscall3(SYS_CHMOD, (usize)path, len, mode);
}

static inline int chmod(const char *path, mode_t mode) {
    return (int)chmod2(path, strlen(path), (usize)mode);
}

/* fchmod(fd, mode): 按 fd 改权限位（省一次路径解析，也不受文件被改名影响）。
 * 特权检查同 chmod：root 或文件属主。pipe/终端/只读 rootfs 的 fd 返回 -1。 */
static inline int fchmod(int fd, mode_t mode) {
    return (int)syscall3(SYS_FCHMOD, (usize)fd, (usize)mode, 0);
}

/* umask(mask): 设置"新建文件/目录默认要去掉的权限位"，返回**旧值**。
 *
 * 内核在 open(O_CREAT) / mkdirat 建东西时按 `mode & ~umask` 收窄权限。
 * 默认 022 → 新文件 0666 落成 0644、新目录 0777 落成 0755。
 * POSIX 常见用法是 `old = umask(0); umask(old);` 就是读一下当前掩码。 */
static inline mode_t umask(mode_t mask) {
    return (mode_t)syscall3(SYS_UMASK, (usize)mask, 0, 0);
}

/* fchmodat(dirfd, path, mode, flags): 相对目录 fd 改权限。
 * 目前不接受 AT_SYMLINK_NOFOLLOW（内核明确返回 -1，而不是静默忽略 flag）。 */
static inline int fchmodat(int dirfd, const char *path, mode_t mode, int flags) {
    return (int)syscall6(SYS_FCHMODAT, (usize)(isize)dirfd, (usize)path,
                         (usize)strlen(path), (usize)mode, (usize)flags, 0);
}

/*
 * access(path, amode) 的 amode 位，取值与 Linux/POSIX 一致。
 * unistd.h 里也有一份（#ifndef 保护，两边同时包含不会冲突）。
 */
#ifndef F_OK
#define F_OK 0   /* 只查存在性 */
#define X_OK 1   /* 可执行 */
#define W_OK 2   /* 可写 */
#define R_OK 4   /* 可读 */
#endif

/*
 * access(path, amode): 以**真实 uid/gid** 检查访问权限（内核 syscall 实现）。
 *   F_OK=0 只查存在; R_OK=4 / W_OK=2 / X_OK=1 可按位或。
 * 与 open 的分野：open 按有效身份(euid)放行，access 按真实身份(ruid)，
 * 所以 setuid/su 过的程序用它才能得到"调用者本人能否访问"的答案。
 * 成功返回 0，失败返回 -1 并按 amode 粗设 errno（F_OK→ENOENT，其余→EACCES）。
 * 注意：内核只有一个返回码，无法区分"不存在"和"权限不足"，这个粗分与
 * 大多数调用方的用法（只看成败）兼容。
 */
static inline isize access2(const char *path, usize len, usize amode) {
    return syscall3(SYS_ACCESS, (usize)path, len, amode);
}

static inline int access(const char *path, int amode) {
    int r = (int)access2(path, strlen(path), (usize)amode);
    if (r != 0) {
        errno = (amode == 0) ? ENOENT : EACCES;
    }
    return r;
}

/* faccessat(dirfd, path, amode, flags): 相对目录 fd 的 access。
 * flags 带 AT_EACCESS 时按**有效身份**(euid)检查，否则按真实身份(ruid)。 */
static inline isize faccessat2(int dirfd, const char *path, usize len,
                               usize amode, usize flags) {
    return syscall6(SYS_FACCESSAT, (usize)(isize)dirfd, (usize)path, len,
                    amode, flags, 0);
}

static inline int faccessat(int dirfd, const char *path, int amode, int flags) {
    int r = (int)faccessat2(dirfd, path, strlen(path), (usize)amode, (usize)flags);
    if (r != 0) {
        errno = (amode == 0) ? ENOENT : EACCES;
    }
    return r;
}

/*
 * chown(path, uid, gid): 修改文件属主。简化模型下仅 root 可改。
 */
static inline isize chown2(const char *path, usize len, usize uid, usize gid) {
    return syscall6(SYS_CHOWN, (usize)path, len, uid, gid, 0, 0);
}

static inline int chown(const char *path, uid_t uid, gid_t gid) {
    return (int)chown2(path, strlen(path), (usize)uid, (usize)gid);
}

/* fchown(fd, uid, gid): 按 fd 改属主，省一次路径解析、也不受改名影响。
 * 特权与 chown 一致（仅 root）；pipe/终端/只读 rootfs 的 fd → -1。
 * 不支持 Linux 的 (uid_t)-1 哨兵，与 chown 保持一致。 */
static inline int fchown(int fd, uid_t uid, gid_t gid) {
    return (int)syscall6(SYS_FCHOWN, (usize)fd, (usize)uid, (usize)gid, 0, 0, 0);
}

/* ---- 目录 / 文件增删 ---- */

/* POSIX 签名: mkdir(path, mode)。内核不记录新建目录的权限, mode 忽略。 */
static inline isize mkdir2(const char *path, usize len) {
    return syscall3(SYS_MKDIR, (usize)path, len, 0);
}

static inline int mkdir(const char *path, mode_t mode) {
    (void)mode;
    return (int)mkdir2(path, strlen(path));
}

/* mkdirat(dirfd, path, mode): 相对目录 fd 建目录，mode 会真正落到权限位上
 * （与 mkdir 不同——那条老路径的 mode 被历史忽略，at 版本按 POSIX 生效）。 */
static inline int mkdirat(int dirfd, const char *path, mode_t mode) {
    return (int)syscall6(SYS_MKDIRAT, (usize)(isize)dirfd, (usize)path,
                         (usize)strlen(path), (usize)mode, 0, 0);
}

/* 
 * 安全临时文件：mkstemp / mkdtemp
 *
 * 为什么需要专门的 API —— 经典的"临时文件攻击"(CWE-377, symlink race):
 *
 *     sprintf(buf, "/tmp/prog.%d", getpid());   // 名字可预测
 *     fp = fopen(buf, "w");                     // 盲目打开
 *
 * 攻击者在程序运行前就把 /tmp/prog.1234 做成**符号链接**指向
 * /etc/shadow（或受害者 home 下的 authorized_keys）。程序一 open 就
 * 顺着链接把目标文件覆盖了——如果是 setuid 程序，就是提权。
 *
 * 两道防线，缺一不可：
 *   1. **原子性**（真正起作用的）：O_CREAT|O_EXCL。目标只要以任何形式存在
 *      （普通文件、目录、**哪怕是 dangling symlink**）就失败，绝不重用。
 *      "先 stat 看存不存在、再 open"是错的——中间有窗口，那正是 TOCTOU。
 *   2. **不可预测的名字**：降低"被提前占位"的概率，只是减少重试次数，
 *      不是安全边界。所以随机数弱一点没关系，O_EXCL 才是底线。
 *
 * 还有第三条容易被忽略：**返回 fd，而不是让你再按名字打开一次**。
 * 按名字重新 fopen 就又把刚消掉的竞态装回去了。
 *
 * 权限固定 0600 / 0700：内容可能敏感，不能等 umask 给到 0666 的窗口期。
 */

/* 随机字符表：去掉 0/O/1/l 这类易混字符（纯可读性，与安全性无关） */
static inline const char *_mkstemp_alphabet(void) {
    return "abcdefghijkmnopqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
}

/* 状态一律放文件作用域：内联函数里"可变的 static 局部变量"同样会触发 TCC
 * 生成原子守卫，所以放函数里也不行。 */
static unsigned long long _mkstemp_seq  = 0;
static unsigned long long _mkstemp_base = 0;

/* splitmix64：把"pid ^ tick ^ 计数器"这种弱种子摊匀，避免低位扎堆 */
static inline unsigned long long _mkstemp_mix64(unsigned long long z) {
    z ^= z >> 30; z *= 0xBF58476D1CE4E5B9ull;
    z ^= z >> 27; z *= 0x94D049BB133111EBull;
    z ^= z >> 31;
    return z;
}

/* 取本次调用的名字种子。
 * 用 syscall3 直接取 pid 而不是 getpid()：fs.h 不该依赖 process.h，
 * 否则"只 include <fs.h>"的翻译单元会缺声明。 */
static inline unsigned long long _mkstemp_next_seed(const void *tmpl) {
    if (_mkstemp_base == 0) {
        unsigned long long s = (unsigned long long)syscall3(SYS_GETPID, 0, 0, 0)
                             * 0x9E3779B97F4A7C15ull;
        s ^= (unsigned long long)syscall3(SYS_GET_TICKS, 0, 0, 0)
                             * 0xC2B2AE3D27D4EB4Full;
        s ^= (unsigned long long)(usize)tmpl * 0x165667B19E3779F9ull;
        _mkstemp_base = _mkstemp_mix64(s) | 1ull;   /* 保证非 0，用于"已播种"判断 */
    }
    return _mkstemp_mix64(_mkstemp_base + (++_mkstemp_seq) * 0x9E3779B97F4A7C15ull);
}

/* 内部实现：把模板里那 6 个 X 换成随机字符并**原子创建**。
 * is_dir=0 → 建文件(返回 fd)；is_dir=1 → 建目录(返回 0)。失败返回 -1。
 * suffixlen 是尾部要保留的后缀长度（mkstemps 用，例如 "fooXXXXXX.pack"）。 */
static inline int _mkstemp_create_sfx(char *tmpl, int is_dir, int suffixlen) {
    usize n = strlen(tmpl);
    if (suffixlen < 0) {
        return -1;
    }
    usize need = (usize)suffixlen + 6;
    if (n < need) {
        return -1;                       /* 模板太短，放不下 6 个 X + 后缀 */
    }
    usize xs = n - need;                 /* 6 个 X 的起始位置 */
    for (usize i = xs; i < xs + 6; ++i) {
        if (tmpl[i] != 'X') {
            return -1;                   /* 必须是 6 个连续的 X（POSIX 要求） */
        }
    }

    /* 种子：pid ^ 内核 tick ^ 调用序号 ^ 模板地址，经 splitmix64 打散。
     * 刻意不碰 rand()/srand()——程序自己 srand(1) 不该让临时名变可预测；
     * 也刻意不碰 __atomic_*——理由见上面 _mkstemp_seq 处的说明。 */
    unsigned long long s = _mkstemp_next_seed(tmpl);

    const char *alpha = _mkstemp_alphabet();
    const usize nchars = strlen(alpha);

    /* 名字撞了就换一个重试。轮数有限：100 次在 62^6 ≈ 5.7e10 的空间里
     * 连续撞车只可能是目录被塞满了或没有写权限。 */
    for (int attempt = 0; attempt < 100; ++attempt) {
        unsigned long long r = s + (unsigned long long)attempt * 0x94D049BB133111EBull;
        for (int i = 5; i >= 0; --i) {
            tmpl[xs + (usize)i] = alpha[r % nchars];
            r /= nchars;
        }

        if (is_dir) {
            if (mkdirat(AT_FDCWD, tmpl, 0700) == 0) {
                return 0;
            }
        } else {
            int fd = (int)open(tmpl, O_CREAT | O_EXCL | O_RDWR, 0600);
            if (fd >= 0) {
                return fd;
            }
        }
    }
    return -1;
}

/* mkstemp: 原子创建唯一临时文件，返回**已打开的可读写 fd**（用完记得 close）。
 * 模板形如 "/tmp/fooXXXXXX"，末尾 6 个 X 会被替换成实际名字（就地修改）。
 * 失败返回 -1，此时模板内容未定义（POSIX 同样不保证）。 */
static inline int mkstemp(char *tmpl) {
    return _mkstemp_create_sfx(tmpl, 0, 0);
}

/* mkdtemp: 同上但建目录（mode 0700）。成功返回 tmpl，失败返回 NULL。 */
static inline char *mkdtemp(char *tmpl) {
    return _mkstemp_create_sfx(tmpl, 1, 0) == 0 ? tmpl : (char *)0;
}

/* mkstemps: 6 个 X **后面还带固定后缀**的版本。glibc/BSD 扩展，POSIX 没有，
 * 但 git 的 tempfile.c 用它（"XXXXXX.pack"/"XXXXXX.lock" 这种命名）。
 * 例："obj/tmp_objXXXXXX.pack" + suffixlen=5 → X 在前面被替换，".pack" 保留。
 * 语义与 mkstemp 完全一样（O_EXCL 原子性底线的理由见上面那段），
 * 只是"X 的结束位置"从 len 变成 len-suffixlen。 */
static inline int mkstemps(char *tmpl, int suffixlen) {
    return _mkstemp_create_sfx(tmpl, 0, suffixlen);
}

static inline isize unlink2(const char *path, usize len) {
    return syscall3(SYS_UNLINK, (usize)path, len, 0);
}

static inline int unlink(const char *path) {
    return (int)unlink2(path, strlen(path));
}

/* unlinkat(dirfd, path, flags):
 *   flags=0            → 删文件（等价 unlink）
 *   flags=AT_REMOVEDIR → 删目录（等价 rmdir）
 * 其他值内核一律拒绝（POSIX 只定义这两个）。 */
static inline int unlinkat(int dirfd, const char *path, int flags) {
    return (int)syscall6(SYS_UNLINKAT, (usize)(isize)dirfd, (usize)path,
                         (usize)strlen(path), (usize)flags, 0, 0);
}

static inline isize rmdir2(const char *path, usize len) {
    return syscall3(SYS_RMDIR, (usize)path, len, 0);
}

static inline int rmdir(const char *path) {
    return (int)rmdir2(path, strlen(path));
}

static inline isize remove_recursive2(const char *path, usize len) {
    return syscall3(SYS_REMOVE_RECURSIVE, (usize)path, len, 0);
}

static inline isize remove_recursive(const char *path) {
    return remove_recursive2(path, strlen(path));
}

/* fcntl 必须是**变参**：POSIX 里 F_GETFL / F_GETFD 这类"查询"命令只传两个参数，
 * 而 F_SETFL / F_SETFD / F_DUPFD 带第三个。固定三参版本会让
 *     fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
 * 这种写法直接编不过（内层只给了两个实参）—— zlib 的 gzlib.c 就是这么写的。
 *
 * 只有查询类命令才跳过 va_arg。目前内核只实现 F_GETFL / F_SETFL，
 * **以后新增 F_GETFD 之类时记得同步这个 switch**，否则会读到栈上的垃圾当 arg。
 * （变参函数编译器不检查实参个数，传错只能靠这里的白名单兜。） */
static inline int fcntl(int fd, int cmd, ...) {
    va_list ap;
    int arg = 0;
    switch (cmd) {
    case F_GETFL:
        break;                     /* 查询类：没有第三个实参 */
    default:
        va_start(ap, cmd);
        arg = va_arg(ap, int);
        va_end(ap);
        break;
    }
    return (int)syscall3(SYS_FCNTL, (usize)fd, (usize)cmd, (usize)arg);
}

static inline off_t lseek(int fd, off_t offset, int whence) {
    return (off_t)syscall3(SYS_LSEEK, (usize)fd, (usize)offset, (usize)whence);
}

static inline int ftruncate(int fd, off_t length) {
    return (int)syscall3(SYS_FTRUNCATE, (usize)fd, (usize)length, 0);
}

static inline int fsync(int fd) {
    return (int)syscall3(SYS_FSYNC, (usize)fd, 0, 0);
}

/* fdatasync(fd): 只保证数据落盘（不含 metadata）。本内核的 fsync 已经是
 * "数据缓存→inode→bitmap→journal" 的全量刷盘，粒度比 fdatasync 更粗，
 * 所以直接 alias 过去即可 —— 教学系统没必要为省一次元数据写再分一套路径。 */
static inline int fdatasync(int fd) {
    return fsync(fd);
}

static inline isize truncate2(const char *path, usize len, usize length) {
    return syscall3(SYS_TRUNCATE, (usize)path, len, length);
}

static inline int truncate(const char *path, off_t length) {
    return (int)truncate2(path, strlen(path), (usize)length);
}

static inline isize rename2(const char *oldpath, usize oldlen, const char *newpath, usize newlen) {
    return syscall6(SYS_RENAME, (usize)oldpath, oldlen, (usize)newpath, newlen, 0, 0);
}

static inline int rename(const char *oldpath, const char *newpath) {
    return (int)rename2(oldpath, strlen(oldpath), newpath, strlen(newpath));
}

/* renameat(olddirfd, oldpath, newdirfd, newpath): 两侧各自相对自己的 dirfd。
 * 两个 dirfd 可以不同，也可以任意一个是 AT_FDCWD；路径是绝对路径时该侧的
 * dirfd 被忽略。跨挂载点的 rename 仍由底层拒绝。 */
static inline int renameat(int olddirfd, const char *oldpath,
                           int newdirfd, const char *newpath) {
    return (int)syscall6(SYS_RENAMEAT,
                         (usize)(isize)olddirfd, (usize)oldpath, (usize)strlen(oldpath),
                         (usize)(isize)newdirfd, (usize)newpath, (usize)strlen(newpath));
}

/* ---- 链接 ---- */

static inline isize symlink2(const char *target, usize target_len,
                             const char *linkpath, usize link_len) {
    return syscall6(SYS_SYMLINK, (usize)target, target_len,
                    (usize)linkpath, link_len, 0, 0);
}

static inline int symlink(const char *target, const char *linkpath) {
    return (int)symlink2(target, strlen(target), linkpath, strlen(linkpath));
}

/* POSIX: 返回实际写入字节数，readlink 不写 NUL */
static inline ssize_t readlink(const char *path, char *buf, size_t bufsiz) {
    return (ssize_t)syscall6(SYS_READLINK, (usize)path, strlen(path),
                             (usize)buf, (usize)bufsiz, 0, 0);
}

static inline isize link2(const char *oldpath, usize oldlen,
                          const char *newpath, usize newlen) {
    return syscall6(SYS_LINK, (usize)oldpath, oldlen,
                    (usize)newpath, newlen, 0, 0);
}

static inline int link(const char *oldpath, const char *newpath) {
    return (int)link2(oldpath, strlen(oldpath), newpath, strlen(newpath));
}

/* linkat(olddirfd, oldpath, newdirfd, newpath, flags): 两侧各自相对 dirfd。
 *
 * flags 只接受 0。POSIX 的 AT_SYMLINK_FOLLOW（跟随源 symlink）没有实现——
 * 内核 ABI 的 6 个参数被两个路径 + 两个 dirfd 用满了，没地方放 flags。
 * 与其静默忽略，不如明确拒绝：调用方至少能发现语义没生效。 */
static inline int linkat(int olddirfd, const char *oldpath,
                         int newdirfd, const char *newpath, int flags) {
    if (flags != 0) {
        return -1;
    }
    return (int)syscall6(SYS_LINKAT,
                         (usize)(isize)olddirfd, (usize)oldpath, (usize)strlen(oldpath),
                         (usize)(isize)newdirfd, (usize)newpath, (usize)strlen(newpath));
}

#ifdef __cplusplus
}
#endif
