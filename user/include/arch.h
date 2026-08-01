#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include "syscall.h"

static inline void shutdown(){
    syscall3(SYS_SHUTDOWN,0,0,0);
}


static inline isize get_time(){
    return syscall3(SYS_GET_TIME,0,0,0);
}
/* ---- 时钟节拍 ---- */

static inline usize get_ticks(void) {
    return syscall3(SYS_GET_TICKS, 0, 0, 0);
}

/* 单调时间(微秒, 自启动起)——ntpdate 的本地假时钟源 */
static inline isize get_time_us(void) {
    return syscall3(SYS_GET_TIME_US, 0, 0, 0);
}

static inline isize hartid(){
    return syscall3(SYS_HARTID,0,0,0);
}

/* 校准内核墙钟: epoch_us = 此刻的绝对 Unix 时间(微秒)。
 * ntpdate 联网取时后调用; 之后 time()/stat 时间戳由内核单调累加。 */
static inline isize set_wall_clock(usize epoch_us) {
    return syscall3(SYS_SET_WALL_CLOCK, epoch_us, 0, 0);
}

#ifdef __cplusplus
}
#endif
