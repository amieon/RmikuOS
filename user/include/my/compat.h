#pragma once
#include "../include/syscall.h"

using size_t    = unsigned long;
using ptrdiff_t = long;


#ifdef NDEBUG
    #define assert(expr) ((void)0)
#else
    // 简易 assert 失败处理:打印信息 + exit(SYS_EXIT=0)
    inline void __assert_fail_msg(const char* expr, const char* file, int line) {
        auto puts_ = [](const char* s) {
            unsigned long n = 0; while (s[n]) n++;
            syscall3(SYS_WRITE , 1, (unsigned long)s, n);
        };
        puts_("assertion failed: ");
        puts_(expr);
        puts_(" at ");
        puts_(file);
        puts_("\n");
        // 行号简单打印
        char buf[16]; int k = 0; int v = line;
        if (v == 0) buf[k++] = '0';
        char tmp[16]; int t = 0;
        while (v > 0) { tmp[t++] = char('0' + v % 10); v /= 10; }
        while (t > 0) buf[k++] = tmp[--t];
        buf[k++] = '\n'; buf[k] = 0;
        puts_(buf);
        syscall3(SYS_EXIT , 1, 0, 0);
    }
    #define assert(expr) \
        do { if (!(expr)) __assert_fail_msg(#expr, __FILE__, __LINE__); } while (0)
#endif

namespace mv {
    template <typename T> struct remove_ref       { using type = T; };
    template <typename T> struct remove_ref<T&>   { using type = T; };
    template <typename T> struct remove_ref<T&&>  { using type = T; };

    template <typename T>
    typename remove_ref<T>::type&& move(T&& x) {
        return static_cast<typename remove_ref<T>::type&&>(x);
    }
}


#ifndef MY_PLACEMENT_NEW_DEFINED
#define MY_PLACEMENT_NEW_DEFINED
inline void* operator new(unsigned long, void* p) noexcept { return p; }
inline void* operator new[](unsigned long, void* p) noexcept { return p; }
#endif