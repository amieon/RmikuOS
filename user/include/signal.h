#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#include "types.h"
#include "syscall.h"

#define SIGINT 2

void kill(isize pid, usize sig){
    syscall3(SYS_KILL, pid, sig, 0);
}

#ifdef __cplusplus
}
#endif
