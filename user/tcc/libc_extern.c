/* libc_extern.c —— 具现化 RmikuOS 头内联 libc 为真实链接符号
 *
 * 背景: TCC 编译用户程序时, 头里的 static inline 函数(printf 等)不会被内联
 *       展开, 而是生成对外部符号的调用; RmikuOS 的 libc 全靠头内联, 没有
 *       真实符号 -> "unresolved reference to 'printf'"。
 * 方案: 先把全部头内联函数重命名(__rmiku_*)【再 include】, 让头展开时
 *       所有 static inline 定义改名; 宿主 gcc 内联消化内部依赖链; 最后
 *       为公共 API 生成非 static 转发 -> 打进 libc.a 供 TCC 链接解析。
 * 注意: 本文件由宿主 riscv64-gcc 编译(不是 TCC); 重命名宏只在此翻译单元生效。
 */
#include <stdarg.h>

/* ---- ① 重命名: 所有头内 static inline 函数(8 头全覆盖) ---- */
#define __pf_putc __rmiku___pf_putc
#define __pf_puts __rmiku___pf_puts
#define __pf_u64 __rmiku___pf_u64
#define __pf_i64 __rmiku___pf_i64
#define __pf_float __rmiku___pf_float
#define __pf_sci __rmiku___pf_sci
#define vfprintf __rmiku_vfprintf
#define __sn_put __rmiku___sn_put
#define vsnprintf __rmiku_vsnprintf
#define fprintf __rmiku_fprintf
#define printf __rmiku_printf
#define vprintf __rmiku_vprintf
#define sprintf __rmiku_sprintf
#define snprintf __rmiku_snprintf
#define putchar __rmiku_putchar
#define getchar __rmiku_getchar
#define puts __rmiku_puts
#define setvbuf __rmiku_setvbuf
#define fileno __rmiku_fileno
#define fgets __rmiku_fgets
#define sscanf __rmiku_sscanf
#define perror __rmiku_perror
#define rename_file __rmiku_rename_file
#define atoi __rmiku_atoi
#define __init_stdin __rmiku___init_stdin
#define __init_stdout __rmiku___init_stdout
#define __init_stderr __rmiku___init_stderr
#define _mode_flags __rmiku__mode_flags
#define fopen __rmiku_fopen
#define fdopen __rmiku_fdopen
#define remove __rmiku_remove
#define _flushbuf __rmiku__flushbuf
#define fclose __rmiku_fclose
#define _fillbuf __rmiku__fillbuf
#define fgetc __rmiku_fgetc
#define fread __rmiku_fread
#define fputc __rmiku_fputc
#define fwrite __rmiku_fwrite
#define fflush __rmiku_fflush
#define fputs __rmiku_fputs
#define feof __rmiku_feof
#define ferror __rmiku_ferror
#define clearerr __rmiku_clearerr
#define __file_lseek __rmiku___file_lseek
#define ftell __rmiku_ftell
#define fseek __rmiku_fseek
#define rewind __rmiku_rewind
#define ungetc __rmiku_ungetc
#define freopen __rmiku_freopen
#define memmove __rmiku_memmove
#define memcmp __rmiku_memcmp
#define memchr __rmiku_memchr
#define strlen __rmiku_strlen
#define strnlen __rmiku_strnlen
#define strcmp __rmiku_strcmp
#define strcoll __rmiku_strcoll
#define strncmp __rmiku_strncmp
#define strcpy __rmiku_strcpy
#define strncpy __rmiku_strncpy
#define strcat __rmiku_strcat
#define strncat __rmiku_strncat
#define strchr __rmiku_strchr
#define strrchr __rmiku_strrchr
#define strstr __rmiku_strstr
#define strspn __rmiku_strspn
#define strcspn __rmiku_strcspn
#define strpbrk __rmiku_strpbrk
#define strerror __rmiku_strerror
#define abort __rmiku_abort
#define realpath __rmiku_realpath
#define abs __rmiku_abs
#define strtol __rmiku_strtol
#define strtoul __rmiku_strtoul
#define strtoll __rmiku_strtoll
#define strtoull __rmiku_strtoull
#define strtof __rmiku_strtof
#define strtold __rmiku_strtold
#define strtod __rmiku_strtod
#define srand __rmiku_srand
#define rand __rmiku_rand
#define realloc __rmiku_realloc
#define _qswap __rmiku__qswap
#define _qsort __rmiku__qsort
#define qsort __rmiku_qsort
#define mmap __rmiku_mmap
#define munmap __rmiku_munmap
#define mprotect __rmiku_mprotect
#define malloc_align_up __rmiku_malloc_align_up
#define malloc_header_size __rmiku_malloc_header_size
#define malloc_payload __rmiku_malloc_payload
#define malloc_block_from_payload __rmiku_malloc_block_from_payload
#define size_to_sc __rmiku_size_to_sc
#define slab_refill __rmiku_slab_refill
#define slab_alloc __rmiku_slab_alloc
#define slab_free __rmiku_slab_free
#define is_slab __rmiku_is_slab
#define slab_sc __rmiku_slab_sc
#define malloc_find_free __rmiku_malloc_find_free
#define malloc_split_block __rmiku_malloc_split_block
#define malloc_request_chunk __rmiku_malloc_request_chunk
#define malloc_coalesce __rmiku_malloc_coalesce
#define malloc_trim __rmiku_malloc_trim
#define __malloc_unlocked __rmiku___malloc_unlocked
#define malloc __rmiku_malloc
#define __free_unlocked __rmiku___free_unlocked
#define free __rmiku_free
#define malloc_payload_size __rmiku_malloc_payload_size
#define calloc __rmiku_calloc
#define yield __rmiku_yield
#define getpid __rmiku_getpid
#define getppid __rmiku_getppid
#define set_front __rmiku_set_front
#define fork __rmiku_fork
#define waitpid __rmiku_waitpid
#define wait __rmiku_wait
#define sleep __rmiku_sleep
#define atexit __rmiku_atexit
#define exit __rmiku_exit
#define exec2 __rmiku_exec2
#define exec_with_args __rmiku_exec_with_args
#define exec __rmiku_exec
#define getuid __rmiku_getuid
#define geteuid __rmiku_geteuid
#define getgid __rmiku_getgid
#define getegid __rmiku_getegid
#define setuid __rmiku_setuid
#define seteuid __rmiku_seteuid
#define setgid __rmiku_setgid
#define setegid __rmiku_setegid
#define setreuid __rmiku_setreuid
#define setregid __rmiku_setregid
#define getgroups __rmiku_getgroups
#define setgroups __rmiku_setgroups
#define spin_init __rmiku_spin_init
#define spin_lock __rmiku_spin_lock
#define spin_unlock __rmiku_spin_unlock
#define mutex_init __rmiku_mutex_init
#define mutex_lock __rmiku_mutex_lock
#define mutex_unlock __rmiku_mutex_unlock
#define write __rmiku_write
#define read __rmiku_read
#define put_char __rmiku_put_char
#define create2 __rmiku_create2
#define create __rmiku_create
#define open2 __rmiku_open2
#define open __rmiku_open
#define open_create __rmiku_open_create
#define close __rmiku_close
#define set_echo __rmiku_set_echo

/* ---- ② include: 必须在重命名宏生效期间展开, 否则头里的定义用真名,
 *                与后面的转发冲突(redefinition of 'printf') ---- */
#include <stdio.h>    /* 带入 file.h: FILE/_stdout/fopen 等 */
#include <string.h>
#include <stdlib.h>
#include <mem.h>
#include <process.h>
#include <lock.h>
#include <io.h>

/* ---- ③ 取消重命名(转发函数要用真名) ---- */
#undef __pf_putc
#undef __pf_puts
#undef __pf_u64
#undef __pf_i64
#undef __pf_float
#undef __pf_sci
#undef vfprintf
#undef __sn_put
#undef vsnprintf
#undef fprintf
#undef printf
#undef vprintf
#undef sprintf
#undef snprintf
#undef putchar
#undef getchar
#undef puts
#undef setvbuf
#undef fileno
#undef fgets
#undef sscanf
#undef perror
#undef rename_file
#undef atoi
#undef __init_stdin
#undef __init_stdout
#undef __init_stderr
#undef _mode_flags
#undef fopen
#undef fdopen
#undef remove
#undef _flushbuf
#undef fclose
#undef _fillbuf
#undef fgetc
#undef fread
#undef fputc
#undef fwrite
#undef fflush
#undef fputs
#undef feof
#undef ferror
#undef clearerr
#undef __file_lseek
#undef ftell
#undef fseek
#undef rewind
#undef ungetc
#undef freopen
#undef memmove
#undef memcmp
#undef memchr
#undef strlen
#undef strnlen
#undef strcmp
#undef strcoll
#undef strncmp
#undef strcpy
#undef strncpy
#undef strcat
#undef strncat
#undef strchr
#undef strrchr
#undef strstr
#undef strspn
#undef strcspn
#undef strpbrk
#undef strerror
#undef abort
#undef realpath
#undef abs
#undef strtol
#undef strtoul
#undef strtoll
#undef strtoull
#undef strtof
#undef strtold
#undef strtod
#undef srand
#undef rand
#undef realloc
#undef _qswap
#undef _qsort
#undef qsort
#undef mmap
#undef munmap
#undef mprotect
#undef malloc_align_up
#undef malloc_header_size
#undef malloc_payload
#undef malloc_block_from_payload
#undef size_to_sc
#undef slab_refill
#undef slab_alloc
#undef slab_free
#undef is_slab
#undef slab_sc
#undef malloc_find_free
#undef malloc_split_block
#undef malloc_request_chunk
#undef malloc_coalesce
#undef malloc_trim
#undef __malloc_unlocked
#undef malloc
#undef __free_unlocked
#undef free
#undef malloc_payload_size
#undef calloc
#undef yield
#undef getpid
#undef getppid
#undef set_front
#undef fork
#undef waitpid
#undef wait
#undef sleep
#undef atexit
#undef exit
#undef exec2
#undef exec_with_args
#undef exec
#undef getuid
#undef geteuid
#undef getgid
#undef getegid
#undef setuid
#undef seteuid
#undef setgid
#undef setegid
#undef setreuid
#undef setregid
#undef getgroups
#undef setgroups
#undef spin_init
#undef spin_lock
#undef spin_unlock
#undef mutex_init
#undef mutex_lock
#undef mutex_unlock
#undef write
#undef read
#undef put_char
#undef create2
#undef create
#undef open2
#undef open
#undef open_create
#undef close
#undef set_echo
/* ============ stdio ============ */
int printf(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = __rmiku_vfprintf(__rmiku___init_stdout(), fmt, ap);
    va_end(ap); return r;
}
int vprintf(const char *fmt, va_list ap) { return __rmiku_vfprintf(__rmiku___init_stdout(), fmt, ap); }
int fprintf(FILE *fp, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = __rmiku_vfprintf(fp, fmt, ap);
    va_end(ap); return r;
}
int vfprintf(FILE *fp, const char *fmt, va_list ap) { return __rmiku_vfprintf(fp, fmt, ap); }
int sprintf(char *str, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = __rmiku_vsnprintf(str, (usize)-1, fmt, ap);
    va_end(ap); return r;
}
int snprintf(char *str, usize size, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = __rmiku_vsnprintf(str, size, fmt, ap);
    va_end(ap); return r;
}
int vsnprintf(char *str, usize cap, const char *fmt, va_list ap) { return __rmiku_vsnprintf(str, cap, fmt, ap); }
int puts(const char *s) { return __rmiku_puts(s); }
int putchar(int ch) { return __rmiku_putchar(ch); }
int getchar(void) { return __rmiku_getchar(); }
int fputs(const char *s, FILE *fp) { return __rmiku_fputs(s, fp); }
int fputc(int c, FILE *fp) { return __rmiku_fputc(c, fp); }
int fflush(FILE *fp) { return __rmiku_fflush(fp); }
int feof(FILE *fp) { return __rmiku_feof(fp); }
int ferror(FILE *fp) { return __rmiku_ferror(fp); }
int fileno(FILE *fp) { return __rmiku_fileno(fp); }
int setvbuf(FILE *fp, char *buf, int mode, size_t size) { return __rmiku_setvbuf(fp, buf, mode, size); }
void perror(const char *msg) { __rmiku_perror(msg); }
int atoi(const char *s) { return __rmiku_atoi(s); }

/* ============ file ============ */
FILE *fopen(const char *path, const char *mode) { return __rmiku_fopen(path, mode); }
FILE *fdopen(int fd, const char *mode) { return __rmiku_fdopen(fd, mode); }
int remove(const char *path) { return __rmiku_remove(path); }
int fclose(FILE *fp) { return __rmiku_fclose(fp); }
int fgetc(FILE *fp) { return __rmiku_fgetc(fp); }
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *fp) { return __rmiku_fread(ptr, size, nmemb, fp); }
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *fp) { return __rmiku_fwrite(ptr, size, nmemb, fp); }
long ftell(FILE *fp) { return __rmiku_ftell(fp); }
int fseek(FILE *fp, long offset, int whence) { return __rmiku_fseek(fp, offset, whence); }
void rewind(FILE *fp) { __rmiku_rewind(fp); }
int ungetc(int c, FILE *fp) { return __rmiku_ungetc(c, fp); }
FILE *freopen(const char *path, const char *mode, FILE *fp) { return __rmiku_freopen(path, mode, fp); }
void clearerr(FILE *fp) { __rmiku_clearerr(fp); }

/* ============ stdlib ============ */
void abort(void) { __rmiku_abort(); }
int abs(int x) { return __rmiku_abs(x); }
long strtol(const char *s, char **e, int b) { return __rmiku_strtol(s, e, b); }
unsigned long strtoul(const char *s, char **e, int b) { return __rmiku_strtoul(s, e, b); }
long long strtoll(const char *s, char **e, int b) { return __rmiku_strtoll(s, e, b); }
unsigned long long strtoull(const char *s, char **e, int b) { return __rmiku_strtoull(s, e, b); }
float strtof(const char *s, char **e) { return __rmiku_strtof(s, e); }
long double strtold(const char *s, char **e) { return __rmiku_strtold(s, e); }
double strtod(const char *s, char **e) { return __rmiku_strtod(s, e); }
void srand(unsigned int s) { __rmiku_srand(s); }
int rand(void) { return __rmiku_rand(); }
void *realloc(void *p, size_t s) { return __rmiku_realloc(p, s); }
void qsort(void *b, size_t n, size_t s, int (*c)(const void *, const void *)) { __rmiku_qsort(b, n, s, c); }
char *realpath(const char *path, char *resolved) { return __rmiku_realpath(path, resolved); }

/* ============ mem ============ */
void *malloc(usize size) { return __rmiku_malloc(size); }
void free(void *ptr) { __rmiku_free(ptr); }
void *calloc(usize n, usize size) { return __rmiku_calloc(n, size); }

/* ============ string (memset/memcpy 在 string.o, 不转发) ============ */
size_t strlen(const char *s) { return __rmiku_strlen(s); }
int strcmp(const char *a, const char *b) { return __rmiku_strcmp(a, b); }
int strncmp(const char *a, const char *b, size_t n) { return __rmiku_strncmp(a, b, n); }
char *strcpy(char *dst, const char *src) { return __rmiku_strcpy(dst, src); }
char *strncpy(char *dst, const char *src, size_t n) { return __rmiku_strncpy(dst, src, n); }
char *strcat(char *dst, const char *src) { return __rmiku_strcat(dst, src); }
char *strchr(const char *s, int c) { return __rmiku_strchr(s, c); }
char *strstr(const char *hay, const char *needle) { return __rmiku_strstr(hay, needle); }
char *strerror(int err) { return __rmiku_strerror(err); }
void *memmove(void *dst, const void *src, size_t n) { return __rmiku_memmove(dst, src, n); }

/* ============ io / process（直接 syscall 调用） ============ */
isize write(int fd, const char *buf, usize len) { return __rmiku_write(fd, buf, len); }
isize read(int fd, char *buf, usize len) { return __rmiku_read(fd, buf, len); }
isize close(int fd) { return __rmiku_close(fd); }
isize open(const char *path, usize flags, ...) {
    usize mode = 0;
    if (flags & O_CREAT) {
        va_list ap; va_start(ap, flags);
        mode = va_arg(ap, usize);
        va_end(ap);
    }
    return __rmiku_open(path, flags, mode);
}
/* exit/atexit 用 weak: -run 时 runmain.o 自带强定义(exit 走 __rt_exit 回溯、
 * atexit 走 rt_exitfunc 表), weak 不冲突且被强版覆盖; AOT(无 runmain)用 weak 版。 */
__attribute__((weak)) void exit(int code) { __rmiku_exit(code); }
__attribute__((weak)) int atexit(void (*function)(void)) { return __rmiku_atexit(function); }

/* ============ TCC runmain 支持（.init_array/.fini_array 边界符号） ============
 * runmain.o 的 run_ctors/run_dtors 引用 _init_array_start/_fini_array_start 等,
 * 但 TCC 链接器(tccelf.c)不生成这些符号 -> -run 链接 unresolved(静默失败)。
 * RmikuOS 无构造函数机制 -> 空数组, 循环 0 次; weak 让位给将来可能的强定义。 */
__attribute__((weak)) void (*_init_array_start[])(int, char **, char **) = { 0 };
__attribute__((weak)) void (*_init_array_end[])(int, char **, char **)   = { 0 };
__attribute__((weak)) void (*_fini_array_start[])(void) = { 0 };
__attribute__((weak)) void (*_fini_array_end[])(void)   = { 0 };
