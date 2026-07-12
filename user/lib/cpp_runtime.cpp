

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

