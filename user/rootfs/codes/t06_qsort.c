/* t06: qsort + 函数指针（结构体排序） */
#include "user.h"

int cmp_int(const void *x, const void *y) {
    int a = *(const int *)x, b = *(const int *)y;
    return a < b ? -1 : (a > b ? 1 : 0);
}

struct Item { int key; char tag; };

int cmp_item(const void *x, const void *y) {
    return ((const struct Item *)x)->key - ((const struct Item *)y)->key;
}

int main() {
    int arr[] = {42, 7, 99, -3, 18, 0, 55, 12};
    int n = sizeof(arr) / sizeof(arr[0]);
    qsort(arr, n, sizeof(int), cmp_int);
    printf("sorted: ");
    for (int i = 0; i < n; i++) printf("%d ", arr[i]);
    printf("\n");
    struct Item items[] = {{5,'e'}, {2,'b'}, {9,'i'}, {1,'a'}};
    qsort(items, 4, sizeof(struct Item), cmp_item);
    printf("items: ");
    for (int i = 0; i < 4; i++) printf("%c%d ", items[i].tag, items[i].key);
    printf("\n");
    return 0;
}
