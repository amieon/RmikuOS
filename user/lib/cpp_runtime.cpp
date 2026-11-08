/* cpp_runtime.cpp —— C++ ABI 运行时符号的唯一落脚点。
 * 所有 C++ 程序都链这个 .o(build.py: _build_cpp_real / build_project_dir)。 */
#include "../include/mem.h"

/* 打印落点放在 string.c(它本来就 include stdio.h), 这里只声明,
 * 避免 cpp_runtime.cpp 同时拉进 mem.h + string.h 两套声明。 */
extern "C" void __rk_pure_virtual_fail(void);

void* operator new(unsigned long size)   { return malloc(size); }
void* operator new[](unsigned long size) { return malloc(size); }
void* operator new(unsigned long, void* p) noexcept { return p; }
void* operator new[](unsigned long, void* p) noexcept { return p; }

void operator delete(void* p) noexcept                   { free(p); }
void operator delete[](void* p) noexcept                 { free(p); }
void operator delete(void* p, unsigned long) noexcept    { free(p); }
void operator delete[](void* p, unsigned long) noexcept  { free(p); }

/* ---- __cxa_atexit: 全局/函数内 static 对象的析构登记 ---- */
#define RK_MAX_DTORS 64
struct rk_dtor_entry {
    void (*fn)(void *);
    void *arg;
    void *dso;
};
static rk_dtor_entry rk_dtors[RK_MAX_DTORS];
static int rk_ndtor = 0;
static mutex_t rk_dtor_lock = MUTEX_INIT;

/* ---- 函数内 static: "正在构造"由锁表示, 别的线程阻塞而非双跑 ---- */
static mutex_t rk_guard_lock = MUTEX_INIT;

extern "C" {

void *__dso_handle = 0;

int __cxa_atexit(void (*fn)(void *), void *arg, void *dso) {
    mutex_lock(&rk_dtor_lock);
    if (rk_ndtor >= RK_MAX_DTORS) {
        mutex_unlock(&rk_dtor_lock);
        return -1;
    }
    rk_dtors[rk_ndtor].fn = fn;
    rk_dtors[rk_ndtor].arg = arg;
    rk_dtors[rk_ndtor].dso = dso;
    rk_ndtor++;
    mutex_unlock(&rk_dtor_lock);
    return 0;
}

/* string.c 的 exit() 用 weak 钩子调用; 纯 C 程序没有强定义, 会跳过。 */
void __rk_run_cxa_dtors(void) {
    while (rk_ndtor > 0) {
        rk_dtor_entry e = rk_dtors[--rk_ndtor];
        e.fn(e.arg);
    }
}

void __cxa_pure_virtual() {
    __rk_pure_virtual_fail();
    for (;;) {}
}

int __cxa_guard_acquire(void *g) {
    mutex_lock(&rk_guard_lock);
    if (*(volatile unsigned char *)g) {   /* 锁内复查, 兜住 fast path 陈旧读 */
        mutex_unlock(&rk_guard_lock);
        return 0;
    }
    return 1;                              /* 持锁返回: 构造过程整个在锁内 */
}

void __cxa_guard_release(void *g) {
    *(volatile unsigned char *)g = 1;
    mutex_unlock(&rk_guard_lock);
}

void __cxa_guard_abort(void *) {
    mutex_unlock(&rk_guard_lock);          /* guard 保持 0, 下次重试 */
}

}
