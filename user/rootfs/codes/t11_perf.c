/* t11: 纯计算压力测试（质数 + 排序 + 求和） */
#include "user.h"

int is_prime(int n) {
    if (n < 2) return 0;
    for (int i = 2; i * i <= n; i++) if (n % i == 0) return 0;
    return 1;
}

int main() {
    int count = 0, sum = 0;
    for (int i = 2; i < 500; i++) if (is_prime(i)) { count++; sum += i; }
    printf("primes < 500: %d, sum = %d\n", count, sum);
    int a[64];
    for (int i = 0; i < 64; i++) a[i] = (i * 37 + 11) % 97;
    for (int i = 0; i < 63; i++) for (int j = i + 1; j < 64; j++)
        if (a[j] < a[i]) { int t = a[i]; a[i] = a[j]; a[j] = t; }
    printf("bubble sorted[0..4] = %d %d %d %d %d\n", a[0], a[1], a[2], a[3], a[4]);
    double s = 0.0;
    for (int i = 1; i <= 100000; i++) s += 1.0 / (double)i;
    printf("harmonic(100000) = %.6f\n", s);
    return 0;
}
