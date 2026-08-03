/* t12: 综合——结构体+字符串+排序+数学 组合 */
#include "user.h"
#include <math.h>

struct Word { char text[24]; int len; };

int cmp_word(const void *x, const void *y) {
    return strcmp(((const struct Word *)x)->text, ((const struct Word *)y)->text);
}

int main() {
    const char *raw[] = {"banana", "apple", "cherry", "date", "elderberry", "fig"};
    struct Word words[6];
    int n = 6;
    for (int i = 0; i < n; i++) {
        strcpy(words[i].text, raw[i]);
        words[i].len = strlen(raw[i]);
    }
    qsort(words, n, sizeof(struct Word), cmp_word);
    printf("sorted words:\n");
    int total_len = 0;
    for (int i = 0; i < n; i++) { total_len += words[i].len; printf("  %-12s len=%d\n", words[i].text, words[i].len); }
    printf("avg len = %.1f, max sqrt(avg) = %.3f\n", (double)total_len / n, sqrt((double)total_len / n));
    return 0;
}
