

#include "../include/mem.h" 



void* operator new(unsigned long size)   { return malloc(size); }
void* operator new[](unsigned long size) { return malloc(size); }


void* operator new(unsigned long, void* p) noexcept { return p; }
void* operator new[](unsigned long, void* p) noexcept { return p; }


void operator delete(void* p) noexcept                 { free(p); }
void operator delete[](void* p) noexcept               { free(p); }
void operator delete(void* p, unsigned long) noexcept  { free(p); }
void operator delete[](void* p, unsigned long) noexcept { free(p); }


extern "C" {

    void __cxa_pure_virtual() { while (1) {} }
    int  __cxa_guard_acquire(void* g) { return !*(char*)g; }
    void __cxa_guard_release(void* g) { *(char*)g = 1; }
    void __cxa_guard_abort(void*) {}
}


// 裸环境 memcpy / memset（编译器优化可能生成这些调用）
extern "C" void* memcpy(void* dst, const void* src, unsigned long n) {
    char* d = (char*)dst;
    const char* s = (const char*)src;
    for (unsigned long i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

extern "C" void* memset(void* dst, int c, unsigned long n) {
    char* d = (char*)dst;
    for (unsigned long i = 0; i < n; i++) d[i] = (char)c;
    return dst;
}s