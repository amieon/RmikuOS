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

static inline isize hartid(){
    return syscall3(SYS_HARTID,0,0,0);
}

#ifdef __cplusplus
}
#endif
