#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include "syscall.h"

void shutdown(){
    syscall3(SYS_SHUTDOWN,0,0,0);
}


isize get_time(){
    return syscall3(SYS_GET_TIME,0,0,0);
}

isize hartid(){
    return syscall3(SYS_HARTID,0,0,0);
}

#ifdef __cplusplus
}
#endif
