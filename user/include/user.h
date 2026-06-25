#pragma once

/*
 * user.h —— 用户态库总入口。
 *
 * 用户程序只需 #include "user.h",即可获得全部用户态接口。
 * 本文件不放任何实现,只按依赖层次汇总各子模块:
 *
 *   types.h     基础类型(usize / isize)
 *   syscall.h   系统调用号 + syscall3 / syscall6
 *   io.h        strlen + read/write + open/close/create + puts/put_char
 *   process.h   exit/fork/waitpid/getpid/yield/sleep + exec
 *   fs.h        dirent/stat + getdents/stat/chdir/getcwd + mkdir/unlink/rmdir
 *   mem.h       PROT_* + mmap/munmap + malloc/free/calloc
 *   lock.h      spinlock / mutex
 *   thread.h    thread_create/exit/join + 栈管理
 *   sched.h     tickets / alpha / sched_proc_stat / get_ticks
 *   ipc.h       pipe / dup2
 *   string.h    trim / copy_str
 *   fmt.h       parse_int/put_int/put_hex/append_* / str_eq / uprintf
 */

#include "types.h"
#include "syscall.h"
#include "io.h"
#include "process.h"
#include "fs.h"
#include "mem.h"
#include "lock.h"
#include "thread.h"
#include "sched.h"
#include "ipc.h"
#include "string.h"
#include "fmt.h"
