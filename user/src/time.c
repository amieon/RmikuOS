#include "arch.h"
#include "stdio.h"

/*
 * 探测 get_time() 到底返回什么：
 *   - 若它返回的是"Unix 纪元秒"(自 1970-01-01 起)，算出的年份应接近 2026；
 *   - 若它返回的是开机单调 tick(自 QEMU 启动起)，算出的年份应接近 1970。
 * 跑起来看 year 即可判定。
 */
int main(void) {
    isize raw = get_time();

    /* 主线判定：把返回值直接当成"自 1970 起的秒数" */
    //printf("%d\n",sizeof(raw));
    long secs = (long)raw;
    long days = secs / 86400L;
    long years = 1970L + days / 365L;

    printf("raw get_time()          = %ld\n", (long)raw);
    printf("==> if epoch seconds    -> ~year %ld\n", years);

    /* 旁证：若它是 ~10MHz 的单调 tick，自开机大约多少秒 */
    printf("    if 10MHz ticks      -> ~%ld sec since boot\n", (long)(raw / 10000000L));

    return 0;
}
