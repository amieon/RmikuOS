#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include "io.h"

#define EOF        (-1)
#define SEEK_SET   0
#define SEEK_CUR   1
#define SEEK_END   2
#define BUFSIZ     512

#define _F_READ    0x01
#define _F_WRITE   0x02
#define _F_APPEND  0x04
#define _F_BIN     0x08
#define _F_EOF     0x10
#define _F_ERR     0x20
#define _F_UNBUF   0x40

typedef struct {
    int fd;
    unsigned char buf[BUFSIZ];
    int pos;
    int end;
    int flags;
    int ungetc;
} FILE;

static FILE _stdin  = {0, {0}, 0, 0, _F_READ, -1};
static FILE _stdout = {1, {0}, 0, 0, _F_WRITE, -1};
static FILE _stderr = {2, {0}, 0, 0, _F_WRITE | _F_UNBUF, -1};

static inline FILE* __init_stdin(void)  { return &_stdin; }
static inline FILE* __init_stdout(void) { return &_stdout; }
static inline FILE* __init_stderr(void) { return &_stderr; }

#define stdin  (__init_stdin())
#define stdout (__init_stdout())
#define stderr (__init_stderr())

/* 静态文件对象池: fopen 从池中分配槽位, fclose 释放回池以便复用。
 * 用 fd == -1 标记空闲槽位(裸机无 malloc, 故为固定大小池)。
 * 注意: 三个标准流(_stdin/_stdout/_stderr)是独立全局对象, 不在池中。 */
#define FILE_POOL_MAX 8
static FILE _file_pool[FILE_POOL_MAX] = {
    {-1}, {-1}, {-1}, {-1}, {-1}, {-1}, {-1}, {-1}
};

static inline int _mode_flags(const char* mode) {
    int f = 0;
    if (*mode == 'r') f |= _F_READ;
    else if (*mode == 'w') f |= _F_WRITE;
    else if (*mode == 'a') f |= _F_WRITE | _F_APPEND;
    mode++;
    if (*mode == '+') { f |= _F_READ | _F_WRITE; mode++; }
    if (*mode == 'b') f |= _F_BIN;
    return f;
}

static inline FILE* fopen(const char* path, const char* mode) {
    int flags = _mode_flags(mode);
    int fd = -1;
    if (flags & _F_APPEND) {
        fd = open_create(path, O_WRONLY | O_CREAT | O_APPEND);
    } else if (flags & _F_WRITE) {
        fd = open_create(path, O_WRONLY | O_CREAT | O_TRUNC);
    } else {
        fd = open(path, O_RDONLY);
    }
    if (fd < 0) return (FILE*)0;
    /* 裸机无 malloc: 从静态池中找一个空闲槽位(fd < 0 表示空闲)。
     * fclose 会把 fd 置回 -1, 因此槽位可以循环复用, 不再是"只增不回收"。 */
    for (int i = 0; i < FILE_POOL_MAX; i++) {
        FILE* fp = &_file_pool[i];
        if (fp->fd < 0) {
            fp->fd = fd; fp->pos = 0; fp->end = 0; fp->flags = flags; fp->ungetc = -1;
            return fp;
        }
    }
    /* 池满: 没有可用槽位 */
    close(fd);
    return (FILE*)0;
}

static inline int _flushbuf(FILE* fp);  /* 前置声明: fclose 需要先冲刷缓冲 */

static inline int fclose(FILE* fp) {
    if (!fp) return EOF;
    /* 冲刷未落盘的写缓冲, 否则不满一个缓冲块且无换行的数据会丢失 */
    if ((fp->flags & _F_WRITE) && !(fp->flags & _F_UNBUF)) _flushbuf(fp);
    if (fp->fd >= 0) close(fp->fd);
    fp->fd = -1; fp->flags = 0; fp->pos = 0; fp->end = 0; fp->ungetc = -1;
    return 0;
}

/* 判断 fd 是否为终端(字符设备): 直接 fstat syscall 读内核 Stat.file_type。
 * 避免 include fs.h。内核 STAT_TYPE_CHAR == 3。 */
static inline int __file_is_tty(int fd) {
    unsigned char kbuf[32];
    if (syscall3(SYS_FSTAT, (usize)fd, (usize)kbuf, 0) < 0) return 0;
    return kbuf[0] == 3;
}

static inline int _fillbuf(FILE* fp) {
    if (fp->flags & (_F_EOF | _F_ERR)) return EOF;
    fp->pos = 0; fp->end = 0;
    if (fp->fd == 0) {
        /* RmikuOS 内核无终端驱动(echo/行缓冲), 由 libc 对 stdin 做
         * "行模式 + 回显", 让 sqlite3/lua 等交互程序有正常终端体验。
         * 仅当 fd 0 是字符设备(终端)时回显; 管道/文件输入不回显。
         * 注: 内核 read 按 "填满 buf 或遇 \\n" 返回, len=1 时逐字符返回。 */
        int is_tty = __file_is_tty(0);
        int i = 0;
        while (i < BUFSIZ) {
            unsigned char c;
            if (read(0, (char*)&c, 1) != 1) break;
            if (c == '\r') c = '\n';
            fp->buf[i++] = c;
            if (is_tty) {
                if (c == 8 || c == 127)      write(1, "\b \b", 3);
                else if (c == '\n')          write(1, "\n", 1);
                else                         write(1, (char*)&c, 1);
            }
            if (c == '\n') break;
        }
        if (i == 0) { fp->flags |= _F_EOF; return EOF; }
        fp->end = i;
        return (unsigned char)fp->buf[fp->pos++];
    }
    isize n = read(fp->fd, (char*)fp->buf, BUFSIZ);
    if (n < 0) { fp->flags |= _F_ERR; return EOF; }
    if (n == 0) { fp->flags |= _F_EOF; return EOF; }
    fp->end = (int)n;
    return (unsigned char)fp->buf[fp->pos++];
}

static inline int fgetc(FILE* fp) {
    if (!fp) return EOF;
    if (fp->ungetc != -1) { int c = fp->ungetc; fp->ungetc = -1; return c; }
    if (fp->flags & _F_UNBUF) {
        unsigned char c;
        if (read(fp->fd, (char*)&c, 1) != 1) { fp->flags |= _F_EOF; return EOF; }
        return c;
    }
    if (fp->pos >= fp->end) return _fillbuf(fp);
    return (unsigned char)fp->buf[fp->pos++];
}

#define getc(fp)      fgetc(fp)
#define putc(c, fp)   fputc((c), (fp))

static inline size_t fread(void* ptr, size_t size, size_t nmemb, FILE* fp) {
    if (!fp || !(fp->flags & _F_READ)) return 0;
    char* dst = (char*)ptr;
    size_t total = size * nmemb;
    size_t got = 0;
    while (got < total) {
        int c = fgetc(fp);
        if (c == EOF) break;
        dst[got++] = (char)c;
    }
    return got / size;
}

static inline int _flushbuf(FILE* fp) {
    if (fp->pos > 0) {
        isize n = write(fp->fd, (char*)fp->buf, fp->pos);
        if (n < 0) { fp->flags |= _F_ERR; return EOF; }
    }
    fp->pos = 0;
    return 0;
}

static inline int fputc(int c, FILE* fp) {
    if (!fp) return EOF;
    if (fp->flags & _F_UNBUF) {
        unsigned char ch = (unsigned char)c;
        if (write(fp->fd, (char*)&ch, 1) != 1) { fp->flags |= _F_ERR; return EOF; }
        return c;
    }
    if (fp->pos >= BUFSIZ) _flushbuf(fp);
    fp->buf[fp->pos++] = (unsigned char)c;
    if (c == '\n' && (fp->flags & _F_WRITE)) _flushbuf(fp);
    return c;
}

static inline size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* fp) {
    if (!fp || !(fp->flags & _F_WRITE)) return 0;
    const char* src = (const char*)ptr;
    size_t total = size * nmemb;
    for (size_t i = 0; i < total; i++) {
        if (fputc((unsigned char)src[i], fp) == EOF) return i / size;
    }
    return nmemb;
}

static inline int fflush(FILE* fp) {
    if (!fp) return EOF;
    if (fp->flags & _F_WRITE) _flushbuf(fp);
    fp->pos = 0; fp->end = 0;
    return 0;
}

static inline int fputs(const char* s, FILE* fp) {
    if (!fp || !s) return EOF;
    while (*s) if (fputc(*s++, fp) == EOF) return EOF;
    return 0;
}

static inline int feof(FILE* fp) { return fp && (fp->flags & _F_EOF); }
static inline int ferror(FILE* fp) { return fp && (fp->flags & _F_ERR); }
static inline void clearerr(FILE* fp) { if (fp) fp->flags &= ~(_F_EOF | _F_ERR); }

/* ---- 定位: 基于 lseek 系统调用, 考虑读/写缓冲与回退字符 ---- */

/* lseek() 由 fs.h 提供, 但 file.h 不能 include fs.h（会把内核版 struct
 * stat/dirent 带进所有用 stdio 的 TU, 与 POSIX 头冲突）。这里直接打原始
 * syscall, 语义与 fs.h 的 lseek(fd, offset, whence) 完全一致。 */
static inline isize __file_lseek(isize fd, isize offset, usize whence) {
    return syscall3(SYS_LSEEK, (usize)fd, (usize)offset, whence);
}

/* ftell: 返回逻辑文件偏移。内核 fd 偏移可能领先/落后于逻辑位置,
 * 需按缓冲内容修正: 写模式加上未落盘的 pos 字节; 读模式减去已读入
 * 但未消费的 (end-pos) 字节, 以及 1 个回退字符。 */
static inline long ftell(FILE* fp) {
    if (!fp || fp->fd < 0) return -1;
    isize raw = __file_lseek(fp->fd, 0, SEEK_CUR);
    if (raw < 0) return -1;
    long pos = (long)raw;
    if (fp->flags & _F_WRITE) {
        pos += fp->pos;
    } else {
        pos -= (fp->end - fp->pos);
        if (fp->ungetc != -1) pos -= 1;
    }
    return pos;
}

static inline int fseek(FILE* fp, long offset, int whence) {
    if (!fp || fp->fd < 0) return -1;
    /* 写模式先把缓冲落盘, 避免定位后覆盖或错位 */
    if ((fp->flags & _F_WRITE) && !(fp->flags & _F_UNBUF)) _flushbuf(fp);
    /* SEEK_CUR 时内核偏移含未消费的读缓冲, 先折算成绝对偏移再定位 */
    if (whence == SEEK_CUR) {
        long cur = ftell(fp);
        if (cur < 0) return -1;
        offset += cur;
        whence = SEEK_SET;
    }
    fp->pos = 0; fp->end = 0; fp->ungetc = -1;
    fp->flags &= ~_F_EOF;
    if (__file_lseek(fp->fd, (isize)offset, (usize)whence) < 0) return -1;
    return 0;
}

static inline void rewind(FILE* fp) {
    if (!fp) return;
    fseek(fp, 0L, SEEK_SET);
    fp->flags &= ~(_F_EOF | _F_ERR);
}

/* ungetc: 只保证 1 个字符回退(与 FILE.ungetc 单槽一致), 符合 C 标准最低保证 */
static inline int ungetc(int c, FILE* fp) {
    if (!fp || c == EOF) return EOF;
    fp->ungetc = (unsigned char)c;
    fp->flags &= ~_F_EOF;
    return (unsigned char)c;
}


static inline FILE* freopen(const char* path, const char* mode, FILE* fp) {
    int flags = _mode_flags(mode);
    int fd = -1;
    if (flags & _F_APPEND)      fd = open_create(path, O_WRONLY | O_CREAT | O_APPEND);
    else if (flags & _F_WRITE)  fd = open_create(path, O_WRONLY | O_CREAT | O_TRUNC);
    else                        fd = open(path, O_RDONLY);
    if (fd < 0) {
        if (fp && fp->fd >= 0) close(fp->fd);
        if (fp) fp->fd = -1;
        return (FILE*)0;
    }
    if (!fp) { close(fd); return fopen(path, mode); }   /* 退化兜底，lua 走不到 */
    if (fp->fd >= 0) close(fp->fd);
    fp->fd = fd; fp->pos = 0; fp->end = 0; fp->flags = flags; fp->ungetc = -1;
    return fp;
}

#ifdef __cplusplus
}
#endif