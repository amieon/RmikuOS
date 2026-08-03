/* t10: long double(128位 quad)——验证 libgcc tf 软浮点 */
#include "user.h"

int main() {
    long double a = 1.5L;
    long double b = 2.25L;
    long double sum = a + b;
    long double prod = a * b;
    long double div = a / b;
    long double d2l = 3.14159;          /* double -> long double */
    long double back = (double)(sum * 100000.0L) / 100000.0L; /* -> double */
    printf("ld 1.5+2.25 = %.4f\n", (double)sum);
    printf("ld 1.5*2.25 = %.4f\n", (double)prod);
    printf("ld 1.5/2.25 = %.6f\n", (double)div);
    printf("d->ld->d     = %.5f\n", (double)d2l);
    printf("roundtrip    = %.5f\n", (double)back);
    if (sum > 3.0L && prod < 4.0L) printf("ld compare ok\n");
    return 0;
}
