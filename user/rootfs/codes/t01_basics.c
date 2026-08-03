/* t01: 基础——循环/数组/函数/取模/位运算 */
#include "user.h"

int square(int x) { return x * x; }
int is_even(int n) { return n % 2 == 0; }

int main() {
    int arr[10];
    int sum = 0;
    for (int i = 0; i < 10; i++) { arr[i] = i * i; sum += arr[i]; }
    printf("sum of squares 0..9 = %d\n", sum);
    printf("square(7) = %d, 42%%4 = %d\n", square(7), 42 % 4);
    printf("even flags: ");
    for (int i = 0; i < 8; i++) printf(is_even(i) ? "E" : "O");
    printf("\n");
    printf("bits of 0x5a = %x, shift 1<<10 = %d\n", 0x5a, 1 << 10);
    return 0;
}
