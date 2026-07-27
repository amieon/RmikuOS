#include "math_utils.h"
#include "stdio.h"

long factorial(int n) {
    if (n <= 1) return 1;
    long result = 1;
    for (int i = 2; i <= n; i++) result *= i;
    return result;
}

long fibonacci(int n) {
    if (n <= 0) return 0;
    if (n == 1) return 1;
    long a = 0, b = 1;
    for (int i = 2; i <= n; i++) {
        long tmp = a + b;
        a = b;
        b = tmp;
    }
    return b;
}

void print_result(const char* name, int n, long result) {
    printf(name);
    printf("(");
    printf("%d", n);
    printf(") = ");
    printf("%d", result);
}
