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
    // 裸机无 malloc，用静态池或全局变量
    // 这里提供一个极简静态池（最多 8 个文件）
    static FILE pool[8];
    static int pool_used = 0;
    if (pool_used >= 8) { close(fd); return (FILE*)0; }
    FILE* fp = &pool[pool_used++];
    fp->fd = fd; fp->pos = 0; fp->end = 0; fp->flags = flags; fp->ungetc = -1;
    return fp;
}

static inline int fclose(FILE* fp) {
    if (!fp) return EOF;
    if (fp->fd >= 0) close(fp->fd);
    fp->fd = -1; fp->flags = 0;
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

#ifdef __cplusplus
}
#endif