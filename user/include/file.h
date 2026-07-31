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

static inline int _fillbuf(FILE* fp) {
    if (fp->flags & (_F_EOF | _F_ERR)) return EOF;
    fp->pos = 0; fp->end = 0;
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