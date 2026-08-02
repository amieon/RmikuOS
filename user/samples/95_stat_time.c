
#include "user.h"
#include <time.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fputs("usage: 94_stat_time <path>\n", stdout);
        return 1;
    }
    struct stat st;
    if (stat(argv[1], &st) < 0) {
        printf("stat failed: %s\n", argv[1]);
        return 1;
    }
    printf("path          : %s\n", argv[1]);
    printf("size          : %lu bytes\n", (unsigned long)st.st_size);
    printf("mtime(epoch)  : %ld\n", (long)st.st_mtime);
    printf("time()(epoch) : %ld\n", (long)time(NULL));

    struct tm tm;
    if (gmtime_r(&st.st_mtime, &tm)) {
        printf("mtime(GMT)    : %04d-%02d-%02d %02d:%02d:%02d\n",
               tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
               tm.tm_hour, tm.tm_min, tm.tm_sec);
    }
    return 0;
}
