/*
 * sys/shm.h —— SysV 共享内存（空壳，故意没有原型）
 *
 * 内核没有 shmget/shmat/shmdt（syscall 表里没有这个语义组），所以这里不声明。
 * 文件存在的唯一理由是挡住工具链 sysroot 里的 glibc 版本。
 */
#ifndef RK_SYS_SHM_H
#define RK_SYS_SHM_H

/* 刻意留空。真要用 SysV shm，先在内核加 syscall 语义组，再回来写原型。 */

#endif /* RK_SYS_SHM_H */
