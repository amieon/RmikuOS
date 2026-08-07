/* t07: 文件 IO——fopen/fwrite/fread 落盘 FAT + 回读 */
#include "user.h"

int main() {
    const char *path = "/fat/tcc_test.txt";
    FILE *fp = fopen(path, "w");
    if (!fp) { printf("open for write failed\n"); return 1; }
    const char *lines[] = {"line one\n", "line two\n", "the quick brown fox\n"};
    for (int i = 0; i < 3; i++) fwrite(lines[i], 1, strlen(lines[i]), fp);
    fclose(fp);
    printf("wrote 3 lines to %s\n", path);
    fp = fopen(path, "r");
    if (!fp) { printf("open for read failed\n"); return 1; }
    char buf[128];
    int n;
    printf("read back:\n");
    while ((n = fread(buf, 1, sizeof(buf) - 1, fp)) > 0) {
        buf[n] = '\0';
        printf("  chunk[%d]: %s", n, buf);
        if (buf[n - 1] != '\n') printf("\n");
    }
    fclose(fp);
    return 0;
}
