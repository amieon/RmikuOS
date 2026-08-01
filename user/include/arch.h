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

/* 墙钟秒(epoch, 1970 起)。未跑 ntpdate 校准前返回 0。 */
static inline isize get_epoch(void) {
    return syscall3(SYS_GET_EPOCH, 0, 0, 0);
}

static inline isize hartid(){
    return syscall3(SYS_HARTID,0,0,0);
}

#ifdef __cplusplus
}
#endif
