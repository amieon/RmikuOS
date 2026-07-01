#pragma once
#include <stdint.h>
#include "../mem.h"

using size_t    = unsigned long;
using ptrdiff_t = long;

#ifdef NDEBUG
    #define assert(expr) ((void)0)
#else
    extern "C" long syscall3(unsigned long, unsigned long, unsigned long, unsigned long);
    inline void __assert_fail_msg(const char* expr, const char* file, int line) {
        auto puts_ = [](const char* s) {
            unsigned long n = 0; while (s[n]) n++;
            syscall3(2 , 1, (unsigned long)s, n);
        };
        puts_("assertion failed: ");
        puts_(expr);
        puts_(" at ");
        puts_(file);
        puts_("\n");
        char buf[16]; int k = 0; int v = line;
        if (v == 0) buf[k++] = '0';
        char tmp[16]; int t = 0;
        while (v > 0) { tmp[t++] = char('0' + v % 10); v /= 10; }
        while (t > 0) buf[k++] = tmp[--t];
        buf[k++] = '\n'; buf[k] = 0;
        puts_(buf);
        syscall3(0 , 1, 0, 0);
    }
    #define assert(expr) \
        do { if (!(expr)) __assert_fail_msg(#expr, __FILE__, __LINE__); } while (0)
#endif

namespace mv {
    // remove_reference
    template <typename T> struct remove_ref       { using type = T; };
    template <typename T> struct remove_ref<T&>   { using type = T; };
    template <typename T> struct remove_ref<T&&>  { using type = T; };

    template <typename T>
    typename remove_ref<T>::type&& move(T&& x) {
        return static_cast<typename remove_ref<T>::type&&>(x);
    }

    template <typename T>
    T&& forward(typename remove_ref<T>::type& x) {
        return static_cast<T&&>(x);
    }
    template <typename T>
    T&& forward(typename remove_ref<T>::type&& x) {
        return static_cast<T&&>(x);
    }

    template <typename T>
    void swap(T& a, T& b) {
        T tmp = move(a);
        a = move(b);
        b = move(tmp);
    }

    template <typename T, typename U>
    struct is_same { static constexpr bool value = false; };
    template <typename T>
    struct is_same<T, T> { static constexpr bool value = true; };

    template <typename T1, typename T2, typename T3>
    struct Tuple3 {
        T1 first; T2 second; T3 third;
        Tuple3() = default;
        Tuple3(T1 a, T2 b, T3 c) : first(a), second(b), third(c) {}
    };

    template <typename T1, typename T2>
    struct Pair {
        T1 first; T2 second;
        Pair() = default;
        Pair(T1 a, T2 b) : first(a), second(b) {}
    };

    template <typename T>
    void sort(T* first, T* last) {
        for (T* i = first + 1; i < last; ++i) {
            T key = move(*i);
            T* j = i - 1;
            while (j >= first && *j > key) {
                *(j + 1) = move(*j);
                --j;
            }
            *(j + 1) = move(key);
        }
    }

    template <typename T>
    T* unique(T* first, T* last) {
        if (first == last) return last;
        T* result = first;
        while (++first != last) {
            if (!(*result == *first)) *(++result) = move(*first);
        }
        return ++result;
    }

    template <typename T>
    void iota(T* first, T* last, T value) {
        while (first != last) *first++ = value++;
    }
}

#ifndef MY_PLACEMENT_NEW_DEFINED
#define MY_PLACEMENT_NEW_DEFINED
inline void* operator new(unsigned long, void* p) noexcept { return p; }
inline void* operator new[](unsigned long, void* p) noexcept { return p; }
#endif

inline void* operator new(unsigned long sz) { return malloc(sz); }
inline void* operator new[](unsigned long sz) { return malloc(sz); }
inline void operator delete(void* p) noexcept { free(p); }
inline void operator delete[](void* p) noexcept { free(p); }