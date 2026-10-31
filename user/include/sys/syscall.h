/*
 * sys/syscall.h —— 空壳（我们的号段在 <syscall.h>，注意没有 sys/ 前缀）
 *
 *   <syscall.h>   本项目自造，SYS_* 从 0 开始编号（kernel/src/syscall/mod.rs 的枚举顺序）
 *   <sys/syscall.h>  Linux/glibc 的惯用位置，语义是 __NR_* 号 + 通用 syscall() 原型
 *
 * 这里留空壳，是为了在 -I 顺序上挡住工具链 sysroot 的 glibc 版。不挡的话，
 * OpenSSL rand_unix.c 的 `#include <sys/syscall.h>`（`#ifdef __linux` 下无条件）
 * 会拿到 Linux 号，和我们的 SYS_* 撞名不同值
 */
#ifndef RK_SYS_SYSCALL_H
#define RK_SYS_SYSCALL_H

#endif /* RK_SYS_SYSCALL_H */
