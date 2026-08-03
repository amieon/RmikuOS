/* t03: 结构体/数组/指针 */
#include "user.h"

struct Point { int x, y; };
struct Student { char name[16]; int id; int score; };

int main() {
    struct Point pts[3] = {{1, 2}, {3, 4}, {5, 6}};
    struct Point *pp = pts;
    for (int i = 0; i < 3; i++)
        printf("pts[%d] = (%d, %d)  dist2 = %d\n", i, pp[i].x, pp[i].y,
               pp[i].x * pp[i].x + pp[i].y * pp[i].y);
    struct Student class[3] = {
        {"miku", 1, 99}, {"rin", 2, 87}, {"len", 3, 92}
    };
    int total = 0;
    for (int i = 0; i < 3; i++) { total += class[i].score; }
    printf("avg score = %d\n", total / 3);
    struct Student *best = &class[0];
    for (int i = 1; i < 3; i++) if (class[i].score > best->score) best = &class[i];
    printf("best = %s (%d)\n", best->name, best->score);
    return 0;
}
