#pragma once

#include "syscall.h"

void shutdown(){
    syscall3(SYS_SHUTDOWN,0,0,0);
}