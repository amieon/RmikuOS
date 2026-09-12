/* time.c —— 显示当前时间
 *
 * 数据源: 内核墙钟(epoch 秒)。需先用 ntpdate 校准,否则显示未校准提示。
 * 纪元秒 -> 年月日 用 Howard Hinnant 的 civil_from_days 算法(纯整数,
 * 无查表,正确处理闰年)。
 */
#include "user.h"

/* 自 1970-01-01 的天数 -> 年/月/日(Howard Hinnant, 公历) */
static void civil_from_days(long z, int *y, int *m, int *d) {
    z += 719468L;
    long era = (z >= 0 ? z : z - 146096L) / 146097L;
    unsigned doe = (unsigned)(z - era * 146097L);          /* [0, 146096] */
    unsigned yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;
    long yr = (long)yoe + era * 400L;
    unsigned doy = doe - (365*yoe + yoe/4 - yoe/100);      /* [0, 365] */
    unsigned mp = (5*doy + 2)/153;                          /* [0, 11] */
    *d = (int)(doy - (153*mp+2)/5 + 1);                     /* [1, 31] */
    *m = (int)(mp < 10 ? mp+3 : mp-9);                      /* [1, 12] */
    *y = (int)(yr + (*m <= 2));
}

static const char *WEEKDAY[] = { "Thu", "Fri", "Sat", "Sun", "Mon", "Tue", "Wed" };

/* 自研 printf 不保证 %02d,手补零 */
static void print2(int v) {
    if (v < 10) putchar('0');
    printf("%d", v);
}

int main(void) {
    time_t t = time(0);
    if (t == 0) {
        printf("time: wall clock not set, run `ntpdate` first\n");
        return 1;
    }

    long secs = (long)t;
    long days = secs / 86400L;
    long rem  = secs % 86400L;
    int hh = (int)(rem / 3600L);
    int mm = (int)((rem % 3600L) / 60L);
    int ss = (int)(rem % 60L);

    int y, mo, d;
    civil_from_days(days, &y, &mo, &d);

    printf("%d-", y); print2(mo); putchar('-'); print2(d);
    putchar(' ');
    print2(hh); putchar(':'); print2(mm); putchar(':'); print2(ss);
    printf(" UTC %s", WEEKDAY[days % 7L < 0 ? days % 7L + 7 : days % 7L]);
    printf("  (epoch %ld)\n", secs);
    return 0;
}