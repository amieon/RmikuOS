#ifdef __cplusplus
extern "C" {
#endif
#define O_RDONLY   0
#define O_WRONLY   1
#define O_RDWR     2
#define O_CREAT    0x40
#define O_EXCL     0x80    /* 与 O_CREAT 连用: 已存在则失败 */
#define O_TRUNC    0x200
#define O_APPEND   0x400 
#define O_ACCMODE  0x3     /* 取访问模式: F_GETFL & O_ACCMODE */
#define O_BINARY   0       /* POSIX 无此标志; TCC 兼容 */


#define O_NONBLOCK  2048
/* "打开即 cloexec"：省掉 open + fcntl 之间被别的线程 fork 的竞态 */
#define O_CLOEXEC   02000000

/* ---- fcntl 命令 ----
 * F_GETFL/F_SETFL 管文件状态标志（访问模式 / O_APPEND / O_NONBLOCK），
 * F_GETFD/F_SETFD 管描述符标志（只有一个 bit：FD_CLOEXEC）。
 * F_DUPFD 复制到「≥ 第三个参数的最小空闲 fd」。 */
#define F_DUPFD             0
#define F_GETFD             1
#define F_SETFD             2
#define F_GETFL             3
#define F_SETFL             4
#define F_DUPFD_CLOEXEC     1030   /* Linux 的值，方便对拍 */

/* 描述符标志位。值就是 1 —— 和 O_WRONLY 撞，但两者不在同一个命令空间，
 * 内核也分开存（PCB.fd_flags 与 PCB.fd_cloexec）。 */
#define FD_CLOEXEC  1


#ifdef __cplusplus
}
#endif
