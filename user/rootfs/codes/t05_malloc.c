/* t05: 动态内存——malloc/calloc/realloc/free */
#include "user.h"

int main() {
    int *a = (int *)malloc(5 * sizeof(int));
    for (int i = 0; i < 5; i++) a[i] = i * 10;
    printf("a: ");
    for (int i = 0; i < 5; i++) printf("%d ", a[i]);
    printf("\n");
    a = (int *)realloc(a, 8 * sizeof(int));
    a[5] = 50; a[6] = 60; a[7] = 70;
    printf("after realloc(8): ");
    for (int i = 0; i < 8; i++) printf("%d ", a[i]);
    printf("\n");
    free(a);
    int *z = (int *)calloc(4, sizeof(int));
    printf("calloc zeros: %d %d %d %d\n", z[0], z[1], z[2], z[3]);
    free(z);
    printf("malloc/free done\n");
    return 0;
}
