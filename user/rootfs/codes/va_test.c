/* va_test.c — 最小变参诊断: 隔离 va_start/va_arg 的正确性 */
#include "user.h"

static int sum(int n, ...) {
    va_list ap;
    va_start(ap, n);
    int s = 0;
    for (int i = 0; i < n; i++)
        s += va_arg(ap, int);
    va_end(ap);
    return s;
}

int main() {
    int r = sum(3, 10, 20, 30);  /* 正确结果 = 60, 作为退出码返回 */
    return r;
}
