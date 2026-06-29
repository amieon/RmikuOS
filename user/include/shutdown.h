#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include "syscall.h"

void shutdown(){
    syscall3(SYS_SHUTDOWN,0,0,0);
}
#ifdef __cplusplus
}
#endif
