#pragma once
/*
 * mini loader 的调用接口：dlopen/dlsym/dlclose/dlerror + RTLD_*，签名和数值
 * 都按 POSIX <dlfcn.h>，所以下面的函数可以直接当标准 API 用。
 *
 * 但头**故意不叫 dlfcn.h**：本项目还没实现完整的 POSIX dlfcn（缺 dladdr /
 * Dl_info / dlvsym / RTLD_NEXT），而所有用户态构建的 -I user/include 都排在
 * 系统头前面。占住标准名会让 OpenSSL 的 crypto/dso/dso_dlfcn.c 和 curl 的
 * configure 截胡到这份残缺版 —— 今天靠 no-dso 没编到它，明天摘开关就炸。
 * 「占住 POSIX 名字 = 永久关掉那个功能」，这里同理，等补齐再改回标准名。
 *
 * 第一版边界（详见 user/lib/dl.c 文件头）：
 *   - 只接绝对路径或相对 cwd 的路径，不做库搜索
 *   - RTLD_LAZY 按 RTLD_NOW 处理（一次全解析，不实现 PLT 懒绑定）
 *   - 只认 SysV .hash，GNU hash 要回链接器加 -Wl,--hash-style=sysv
 *   - 不支持 DT_NEEDED：一个 .so 不能依赖另一个 .so
 */
#ifdef __cplusplus
extern "C" {
#endif

/* 数值照 Linux <dlfcn.h>，别改：第三方可能直接拿这些常量比对。 */
#define RTLD_LAZY   0x00001
#define RTLD_NOW    0x00002
#define RTLD_GLOBAL 0x00100
#define RTLD_LOCAL  0

/* dlsym 的特殊句柄：NULL 与 RTLD_DEFAULT 都表示「查全局符号表」。
 * RTLD_NEXT 刻意不定义 —— 它需要调用者的返回地址，本实现没有这套机制。 */
#define RTLD_DEFAULT ((void *)0)

void *dlopen(const char *path, int flags);
void *dlsym(void *handle, const char *name);
int   dlclose(void *handle);
char *dlerror(void);

/* ---- 自有扩展（rk_ 前缀，不占 POSIX 名） ----
 * 主程序是 -static -no-pie，进程里读不到自己 ELF 的 .dynsym，所以 loader
 * 无从自动知道主程序导出了哪些符号。用这个把要暴露给 .so 的函数登记进
 * 全局表；重定位和 dlsym(NULL, ...) 都查这张表。
 * 同名重复注册会覆盖旧值，返回 0；表满返回 -1（dlerror 有说明）。 */
int rk_dl_register(const char *name, void *addr);

#ifdef __cplusplus
}
#endif
