/*
 * alloca.h —— 栈上分配（POSIX/BSD 扩展，非 C 标准）
 *
 * 编译器内建，不产生任何函数调用：分配在当前栈帧上，
 * 函数返回时自动回收，绝对不能 free。
 */
#ifndef RK_ALLOCA_H
#define RK_ALLOCA_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef alloca
#define alloca(n) __builtin_alloca((unsigned long)(n))
#endif

#ifdef __cplusplus
}
#endif

#endif /* RK_ALLOCA_H */
