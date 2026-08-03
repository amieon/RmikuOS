/* t04: 递归——fib / 阶乘 / 汉诺塔 */
#include "user.h"

int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
long fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }

void hanoi(int n, char from, char to, char via) {
    if (n == 0) return;
    hanoi(n - 1, from, via, to);
    printf("  move %c -> %c\n", from, to);
    hanoi(n - 1, via, to, from);
}

int main() {
    printf("fib(10) = %d, fib(20) = %d\n", fib(10), fib(20));
    printf("10! = %ld\n", fact(10));
    printf("hanoi(3):\n");
    hanoi(3, 'A', 'C', 'B');
    return 0;
}
