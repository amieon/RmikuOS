#include "my/stdcompat.h"

extern "C" int main() {
    int errors = 0;

    // is_same
    if (!std::is_same<int, int>::value) { uprintf("FAIL: is_same<int,int>\n"); errors++; }
    if (std::is_same<int, float>::value) { uprintf("FAIL: is_same<int,float>\n"); errors++; }

    // move
    int a = 5; int b = mv::move(a); if (b != 5) { uprintf("FAIL: move\n"); errors++; }

    // swap
    int x = 1, y = 2; mv::swap(x, y); if (x != 2 || y != 1) { uprintf("FAIL: swap\n"); errors++; }

    // Tuple3 + structured binding
    mv::Tuple3<int, float, char> t(1, 2.5f, 'a');
    auto& [t1, t2, t3] = t;
    
    if (t1 != 1 || t2 != 2.5f || t3 != 'a') { uprintf("FAIL: Tuple3/structured binding\n"); errors++; }

    // Pair
    mv::Pair<int, double> p(3, 3.14); if (p.first != 3 || p.second != 3.14) { uprintf("FAIL: Pair\n"); errors++; }

    // sort
    int arr[] = {5, 2, 8, 1, 9};
    mv::sort(arr, arr + 5);
    if (arr[0] != 1 || arr[4] != 9) { uprintf("FAIL: sort\n"); errors++; }

    // unique
    int arr2[] = {1, 1, 2, 2, 3};
    int* end = mv::unique(arr2, arr2 + 5);
    if ((end - arr2) != 3 || arr2[0] != 1 || arr2[1] != 2 || arr2[2] != 3) { uprintf("FAIL: unique\n"); errors++; }

    // iota
    int arr3[5]; mv::iota(arr3, arr3 + 5, 10);
    if (arr3[0] != 10 || arr3[4] != 14) { uprintf("FAIL: iota\n"); errors++; }

    // forward
    int&& r = 42; int&& r2 = mv::forward<int&&>(r); if (r2 != 42) { uprintf("FAIL: forward\n"); errors++; }

    // 全局 new/delete
    int* pnew = new int(123); if (*pnew != 123) { uprintf("FAIL: new\n"); errors++; }
    delete pnew;
    int* arrnew = new int[4]{1,2,3,4}; if (arrnew[3] != 4) { uprintf("FAIL: new[]\n"); errors++; }
    delete[] arrnew;

    uprintf("compat_v2: %d errors\n", errors);
    return errors;
}