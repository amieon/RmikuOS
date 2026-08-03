/* t08: 数学函数——pow/sqrt/fabs/sin/cos + %f */
#include "user.h"
#include <math.h>

int main() {
    printf("pow(2,10) = %.0f\n", pow(2.0, 10.0));
    printf("sqrt(2) = %.6f\n", sqrt(2.0));
    printf("fabs(-3.5) = %.1f\n", fabs(-3.5));
    printf("sin(pi/2) = %.6f\n", sin(3.141592653589793 / 2.0));
    printf("cos(0) = %.1f\n", cos(0.0));
    printf("floor(3.7) = %.0f, ceil(3.2) = %.0f\n", floor(3.7), ceil(3.2));
    printf("fmod(10,3) = %.1f\n", fmod(10.0, 3.0));
    printf("atan2(1,1) = %.6f (pi/4)\n", atan2(1.0, 1.0));
    double acc = 0.0;
    for (int i = 1; i <= 100; i++) acc += 1.0 / i;
    printf("harmonic(100) = %.6f\n", acc);
    return 0;
}
