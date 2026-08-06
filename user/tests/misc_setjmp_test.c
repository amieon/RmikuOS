#include "test.h"
#include "setjmp.h"

/* 杂项:setjmp / longjmp 非局部跳转 */
int main() {
    TEST_START("misc_setjmp");

    jmp_buf env;
    int r = setjmp(env);
    if (r == 0) {
        /* 第一次到达:发起跳转 */
        longjmp(env, 42);
        FAIL("longjmp 后不应回到此处");
    } else {
        CHECK_EQ(r, 42, "setjmp 返回 longjmp 的第二个参数(42)");
    }

    /* 嵌套场景:longjmp 跳出多层函数 */
    static jmp_buf env2;
    int ok = 0;
    {
        /* 用局部作用域模拟"函数调用后被 longjmp 弹回" */
        r = setjmp(env2);
        if (r == 0) {
            /* 假装在深层函数里跳出来 */
            longjmp(env2, 7);
            FAIL("不应回到这里");
        } else {
            ok = (r == 7);
        }
    }
    CHECK(ok, "longjmp 跳出后 setjmp 返回 7");

    TEST_END();
}
