#ifndef ERRNO_H
#define ERRNO_H
extern int errno;

/* 标准 errno 码。值与 Linux 一致（sqlite3.c/shell.c 本身不引用这些
 * 常量，是给 libc 内部实现用的）。 */
#define EPERM            1
#define ENOENT           2
#define ESRCH            3
#define EINTR            4
#define EIO              5
#define ENXIO            6
#define E2BIG            7
#define ENOEXEC          8
#define EBADF            9
#define ECHILD          10
#define EAGAIN          11
#define ENOMEM          12
#define EACCES          13
#define EFAULT          14
#define EBUSY           16
#define EEXIST          17
#define EXDEV           18
#define ENODEV          19
#define ENOTDIR         20
#define EISDIR          21
#define EINVAL          22
#define ENFILE          23
#define EMFILE          24
#define ENOTTY          25
#define EFBIG           27
#define ENOSPC          28
#define ESPIPE          29
#define EROFS           30
#define EMLINK          31
#define EPIPE           32
#define EDOM            33
#define ERANGE          34
#define ENAMETOOLONG    36
#define ENOSYS          38
#define ENOTEMPTY       39
#define ELOOP           40
#define ENOTSUP         95
#define EOPNOTSUPP      95
#define EAFNOSUPPORT    97   /* 地址家族不支持(inet_pton 对非 AF_INET 的返回) */
#define EWOULDBLOCK     EAGAIN

/* ---- 下面这批本内核永远不会产生，纯属"移植代码会写进 switch/case" ----
 * 例：git 的 connect.c 在函数体里直接比较 ECONNREFUSED；缺定义就是编译错误，
 * 而这块代码在本地 git 里根本不会被调用，所以给值即可，不承诺语义。 */
#define ETXTBSY         26
#define EOVERFLOW       75
#define EILSEQ          84
#define ENOTSOCK        88
#define EMSGSIZE        90
#define ENOPROTOOPT     92  
#define EPROTONOSUPPORT 93
#define EADDRINUSE      98
#define EADDRNOTAVAIL   99
#define ENETDOWN       100
#define ENETUNREACH    101
#define ECONNABORTED   103
#define ECONNRESET     104
#define ENOBUFS        105
#define EISCONN        106
#define ENOTCONN       107
#define ETIMEDOUT      110
#define ECONNREFUSED   111
#define EHOSTUNREACH   113
#define EALREADY       114
#define EINPROGRESS    115
#endif
