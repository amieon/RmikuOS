#include <stdio.h>
#include <setjmp.h>

static jmp_buf env;

int main(void) {
    volatile double f = 0.0;

    printf("[1] before setjmp\n");

    int r = setjmp(env);
    if (r == 0) {
        printf("[2] setjmp first return (0)\n");
        f = 3.14;                       /* 写浮点寄存器(callee-saved) */
        printf("[3] setting float value\n");
        printf("[4] longjmp called\n");
        longjmp(env, 42);
        printf("!!! UNREACHABLE !!!\n");
        return 1;
    } else {
        printf("[5] setjmp second return (%d)\n", r);
        if (f == 3.14) {
            printf("[6] float value survived: %f\n", f);
            printf("[7] all OK\n");
            return 0;
        } else {
            printf("[6] FLOAT CORRUPTED: f=%f (expected 3.140000)\n", f);
            printf("    -> setjmp/longjmp 未保存浮点 callee-saved 寄存器\n");
            return 2;
        }
    }
}
