/*
 * sys/select.h —— select() 与 fd_set
 *
 * 内核只有 poll（SYS_POLL），没有 select。这里用 poll 实现 select：
 * 把 fd_set 位图展开成 struct pollfd[]，poll 返回后再按 revents 压回 fd_set。
 *
 * ## 为什么不把 select 做成 syscall
 * select 和 poll 表达的是同一件事（"这些 fd 里谁准备好了"），
 * 内核只需要理解一种表达。用户态做转换的代价是每次调用多一次 O(nfds) 拷贝，
 * 对这个系统的 fd 规模（个位数）完全无关痛痒。
 *
 * ## FD_SETSIZE = 256
 * fd_set 是**定长位图**，POSIX 允许实现自己定上限。取 256 是因为
 * pollfd 数组直接开在栈上（256 * 8B = 2KB），再大就要担心用户栈。
 * 超过上限时 select 返回 -1 + EINVAL，而不是悄悄截断。
 */
#ifndef RK_SYS_SELECT_H
#define RK_SYS_SELECT_H

#include "../poll.h"      /* struct pollfd / POLLIN / POLLOUT / poll() */
#include "../time.h"      /* struct timeval */
#include "../errno.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef FD_SETSIZE
#define FD_SETSIZE 256
#endif

#define RK_FD_BITS_PER_WORD ((int)(8 * sizeof(unsigned long)))
#define RK_FD_WORDS         (FD_SETSIZE / RK_FD_BITS_PER_WORD)

typedef struct {
    unsigned long bits[RK_FD_WORDS];
} fd_set;

static inline void FD_ZERO(fd_set *set) {
    for (int i = 0; i < RK_FD_WORDS; i++) set->bits[i] = 0UL;
}

static inline void FD_SET(int fd, fd_set *set) {
    if (fd < 0 || fd >= FD_SETSIZE) return;
    set->bits[fd / RK_FD_BITS_PER_WORD] |= 1UL << (fd % RK_FD_BITS_PER_WORD);
}

static inline void FD_CLR(int fd, fd_set *set) {
    if (fd < 0 || fd >= FD_SETSIZE) return;
    set->bits[fd / RK_FD_BITS_PER_WORD] &= ~(1UL << (fd % RK_FD_BITS_PER_WORD));
}

static inline int FD_ISSET(int fd, const fd_set *set) {
    if (fd < 0 || fd >= FD_SETSIZE) return 0;
    return (set->bits[fd / RK_FD_BITS_PER_WORD] >> (fd % RK_FD_BITS_PER_WORD)) & 1UL;
}

/* 返回就绪 fd 的个数；0 = 超时；-1 = 错误（errno 由 poll 设置）。
 * 与 POSIX 一致：三个集合会被**就地改写**，只剩就绪的位。
 * timeout == NULL 表示无限等待。 */
static inline int select(int nfds, fd_set *readfds, fd_set *writefds,
                         fd_set *exceptfds, struct timeval *timeout) {
    if (nfds < 0 || nfds > FD_SETSIZE) { errno = EINVAL; return -1; }

    struct pollfd pfd[FD_SETSIZE];
    int n = 0;

    for (int fd = 0; fd < nfds; fd++) {
        short ev = 0;
        if (readfds  && FD_ISSET(fd, readfds))  ev = (short)(ev | POLLIN);
        if (writefds && FD_ISSET(fd, writefds)) ev = (short)(ev | POLLOUT);
        /* 异常集：poll 没有对应的 "except" 事件位，只能旁听 POLLERR */
        if (exceptfds && FD_ISSET(fd, exceptfds)) ev = (short)(ev | POLLIN);
        if (!ev) continue;
        pfd[n].fd = fd;
        pfd[n].events = ev;
        pfd[n].revents = 0;
        n++;
    }

    int ms = -1;
    if (timeout) {
        if (timeout->tv_sec < 0 || timeout->tv_usec < 0) { errno = EINVAL; return -1; }
        /* 向上取整：500us 不能变成 0（那会立刻超时） */
        ms = (int)(timeout->tv_sec * 1000 + (timeout->tv_usec + 999) / 1000);
    }

    int rc = poll(pfd, (nfds_t)n, ms);
    if (rc < 0) return -1;       /* errno 由 poll 设置 */

    /* rc >= 0 时集合一律先清空，再按 revents 把就绪的位放回去。
     * 超时（rc == 0）也必须清：Linux 内核就是这样写回的，而"留着入参原样返回"
     * 会让调用方照着 FD_ISSET 判断时，把"一个 fd 都没就绪"误读成"这些 fd 都能读"
     * —— select 返回 0 却被当成有数据，接下来 read 阻塞住，症状像丢事件。 */
    if (readfds)   FD_ZERO(readfds);
    if (writefds)  FD_ZERO(writefds);
    if (exceptfds) FD_ZERO(exceptfds);
    if (rc == 0)   return 0;

    int ready = 0;
    for (int i = 0; i < n; i++) {
        short re = pfd[i].revents;
        if (!re) continue;
        ready++;
        /* 对读集合而言，挂断/出错也要报告，否则调用方会一直等不到数据 */
        if (readfds  && (re & (POLLIN | POLLHUP | POLLERR))) FD_SET(pfd[i].fd, readfds);
        if (writefds && (re & POLLOUT))                      FD_SET(pfd[i].fd, writefds);
        if (exceptfds && (re & POLLERR))                     FD_SET(pfd[i].fd, exceptfds);
    }
    return ready;
}

#ifdef __cplusplus
}
#endif

#endif /* RK_SYS_SELECT_H */
