#include "test.h"
#include "dirent.h"

/* 文件系统:opendir/readdir 列目录,能看到刚创建的文件 */
int main() {
    TEST_START("fs_dirent");

    unlink("/tmp/d_f1");
    unlink("/tmp/d_f2");
    int fd1 = open("/tmp/d_f1", O_CREAT | O_RDWR);
    int fd2 = open("/tmp/d_f2", O_CREAT | O_RDWR);
    CHECK(fd1 >= 0 && fd2 >= 0, "创建两个测试文件成功");
    write(fd1, "a", 1);
    write(fd2, "b", 1);
    close(fd1);
    close(fd2);

    DIR *dir = opendir("/tmp");
    CHECK(dir != 0, "opendir(/tmp) 成功");

    int found1 = 0, found2 = 0;
    struct dirent *d;
    while ((d = readdir(dir)) != 0) {
        if (strcmp(d->d_name, "d_f1") == 0) found1 = 1;
        if (strcmp(d->d_name, "d_f2") == 0) found2 = 1;
    }
    CHECK(found1, "readdir 能看到 d_f1");
    CHECK(found2, "readdir 能看到 d_f2");

    CHECK(closedir(dir) == 0, "closedir 成功");

    unlink("/tmp/d_f1");
    unlink("/tmp/d_f2");
    TEST_END();
}
