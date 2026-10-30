#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include "syscall.h"

/* 关机（本内核自己的语义，不是 POSIX 的 socket shutdown） */
static inline void rk_shutdown(void) {
    syscall3(SYS_SHUTDOWN, 0, 0, 0);
}

/* 单调**硬件计数器**（自启动起，不随 ntpdate 校准跳变）。
 *
 * 单位不是秒：riscv64 的 time CSR 跑 10MHz、loongarch 的 rdtime.d 跑 100MHz，
 * 1 秒 = 一千万 / 一亿 次计数。把它当秒用是踩过的坑（nanosleep 因此睡 100ns
 * 就返回）。要秒级时间请用下面的 get_time_us() 除以 1e6。 */
static inline isize rk_get_time(void) {
    return syscall3(SYS_GET_TIME, 0, 0, 0);
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

/* 校准内核墙钟: epoch_us = 此刻的绝对 Unix 时间(微秒)。
 * ntpdate 联网取时后调用; 之后 time()/stat 时间戳由内核单调累加。 */
static inline isize set_wall_clock(usize epoch_us) {
    return syscall3(SYS_SET_WALL_CLOCK, epoch_us, 0, 0);
}

#ifdef __cplusplus
}
#endif
