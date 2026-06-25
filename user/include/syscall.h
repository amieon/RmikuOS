#pragma once

/*
 * syscall.h —— 系统调用层。
 *
 * 内容:
 *   - 全部系统调用号(SYS_*)
 *   - 原始 syscall3 / syscall6 入口(由 syscall_<arch>.S 提供实现)
 *
 * 这是除 types.h 外最底层的头文件。其余所有"对内核发起请求"的包装
 * (io / fs / mem / sched / ipc / thread)都建立在这两个原始入口之上。
 */

#include "types.h"

/* ---- 系统调用号 ---- */

#define SYS_EXIT       0
#define SYS_YIELD      1
#define SYS_WRITE      2
#define SYS_GETPID     3
#define SYS_FORK       4
#define SYS_WAITPID    5
#define SYS_SLEEP      6
#define SYS_EXEC       7
#define SYS_READ       8
#define SYS_OPEN       9
#define SYS_CLOSE      10
#define SYS_GETDENTS   11
#define SYS_CHDIR      12
#define SYS_GETCWD     13
#define SYS_STAT       14
#define SYS_FSTAT      15
#define SYS_THREAD_CREATE            16
#define SYS_THREAD_EXIT              17
#define SYS_THREAD_JOIN              18
#define SYS_MMAP                     19
#define SYS_MUNMAP                   20
#define SYS_SET_THREAD_TICKETS       21
#define SYS_SET_PROCESS_TICKETS      22
#define SYS_SET_MY_TICKETS           23
#define SYS_GET_THREAD_TICKETS       24
#define SYS_GET_PROCESS_TICKETS      25
#define SYS_GET_MY_TICKETS           26
#define SYS_SET_SCHED_ALPHA          27
#define SYS_GET_SCHED_ALPHA          28
#define SYS_GET_PROCESS_SCHED_STAT   29
#define SYS_RESET_SCHED_STAT         30
#define SYS_GET_TICKS                31
#define SYS_PIPE                     32
#define SYS_DUP2                     33
#define SYS_MKDIR                    34
#define SYS_CREATE                   35
#define SYS_UNLINK                   36
#define SYS_RMDIR                    37
#define SYS_REMOVE_RECURSIVE         38

/* ---- 原始系统调用入口(由汇编提供) ---- */

isize syscall3(usize id, usize a0, usize a1, usize a2);

isize syscall6(usize id, usize a0, usize a1, usize a2, usize a3, usize a4, usize a5);
