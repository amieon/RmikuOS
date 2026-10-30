#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include "fs.h"
#include "stdio.h"

/*
 * fake-hwclock：把"当前墙钟"存成一个文件，开机时由内核读回。
 *
 * 这是无 RTC 机型的标准做法（Debian 的 fake-hwclock 包做过一模一样的事）：
 *
 *   开机   kernel/src/drivers/rtc/stamp.rs 读 /var/lib/rtcstamp
 *          → set_wall_clock(值) → 之后 now = 值 + monotonic_us()
 *   校时   ntpdate 拿到真值 → set_wall_clock() → 顺手回写这个文件
 *   关机   每次写入都 fsync，断电也不会丢
 *
 * 路径和格式必须和 stamp.rs 严格一致：十进制秒 + 可有可无的行尾空白。
 * 改路径要同时改两边，否则内核读不到、你只会看到 mtime 还是 1970。
 */
#define RTCSTAMP_PATH "/var/lib/rtcstamp"
#define RTCSTAMP_DIR  "/var/lib"

/* 定期快照间隔（秒）。
 *
 * stamp 存的是"最后一次写它的时刻"，所以**离关机越近越好**。不设上限的话，
 * 开机校时后跑十小时再关，下次开机就慢十小时。
 *
 * 30 分钟是个折中：一次写入要 fsync（rsext4 会走 journal 提交），
 * 对 virtio 盘是几十毫秒量级，半小时一次完全无感；而误差上界被压在 30 分钟内。 */
#define HWCLOCK_STAMP_INTERVAL 1800UL

/* 写入指定时刻（Unix 秒）。成功返回 0。
 *
 * fsync 不能省：这个文件的全部价值就是"下次开机还在"，
 * 只写进页缓存的话，一旦不是正常关机（QEMU 被 kill、真机拔电）就全丢了。 */
static inline int hwclock_stamp_write(unsigned long secs) {
    /* 目录已存在时 mkdir 返回 -1，属正常情况，不检查 */
    mkdir(RTCSTAMP_DIR, 0755);

    int fd = (int)open(RTCSTAMP_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return -1;
    }

    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%lu\n", secs);
    if (n <= 0 || (usize)n >= sizeof(buf)) {
        close(fd);
        return -1;
    }

    isize w = write(fd, buf, (usize)n);
    int rc = -1;
    if (w == (isize)n && fsync(fd) == 0) {
        rc = 0;
    }
    close(fd);
    return rc;
}

/* 读回 stamp。成功返回 0 并把秒数写进 *secs。 */
static inline int hwclock_stamp_read(unsigned long *secs) {
    int fd = (int)open(RTCSTAMP_PATH, O_RDONLY, 0);
    if (fd < 0) {
        return -1;
    }

    char buf[32];
    isize n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) {
        return -1;
    }
    buf[n] = '\0';

    unsigned long v = 0;
    int seen = 0;
    for (isize i = 0; i < n; i++) {
        if (buf[i] >= '0' && buf[i] <= '9') {
            v = v * 10 + (unsigned long)(buf[i] - '0');
            seen = 1;
        } else {
            break;
        }
    }

    if (!seen) {
        return -1;
    }
    *secs = v;
    return 0;
}

#ifdef __cplusplus
}
#endif
