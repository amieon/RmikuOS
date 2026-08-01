#ifndef RMIKU_UTIME_H
#define RMIKU_UTIME_H

/* POSIX <utime.h> - shell.c fileio extension includes it unconditionally
 * and calls utime()/utimes() in writefile(). RmikuOS has no file
 * timestamps, so both return 0 (treated as success). */

#include "sys/types.h"   /* time_t */
#include "time.h"        /* struct timeval */

#ifdef __cplusplus
extern "C" {
#endif

struct utimbuf {
    time_t actime;    /* access time */
    time_t modtime;   /* modification time */
};

static inline int utime(const char *path, const struct utimbuf *times) {
    (void)path; (void)times;
    return 0;   /* no timestamps on RmikuOS */
}

/* POSIX utimes: times[0]=access, times[1]=modification */
static inline int utimes(const char *path, const struct timeval times[2]) {
    (void)path; (void)times;
    return 0;   /* no timestamps on RmikuOS */
}

#ifdef __cplusplus
}
#endif

#endif /* RMIKU_UTIME_H */
