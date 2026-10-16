#include "test.h"

/* 文件系统:symlink/readlink/lstat/hard link 的 POSIX 语义 */
int main() {
    TEST_START("fs_link");

    const char *target = "/tmp/link_target";
    const char *absolute = "/tmp/link_abs";
    const char *relative = "/tmp/link_rel";
    const char *hard = "/tmp/link_hard";
    const char *dangling = "/tmp/link_dangling";
    const char *dir = "/tmp/link_dir";
    const char *alias = "/tmp/link_alias";
    const char *inside = "/tmp/link_dir/inside.txt";

    /* 清场(容忍不存在) */
    unlink(inside);
    unlink(absolute);
    unlink(relative);
    unlink(hard);
    unlink(dangling);
    unlink(alias);
    unlink(target);
    rmdir(dir);

    int fd = open(target, O_CREAT | O_RDWR);
    CHECK(fd >= 0, "创建硬链接目标文件成功");
    CHECK(write(fd, "link-ok", 7) == 7, "写入目标文件内容成功");
    close(fd);

    CHECK(symlink(target, absolute) == 0, "创建绝对路径 symlink 成功");
    CHECK(symlink("link_target", relative) == 0, "创建相对路径 symlink 成功");

    char link_buf[128];
    memset(link_buf, 0, sizeof(link_buf));
    isize n = readlink(absolute, link_buf, sizeof(link_buf) - 1);
    CHECK(n == (isize)strlen(target), "readlink 返回目标长度");
    CHECK_STREQ(link_buf, target, "readlink 返回原样目标字符串");

    struct stat st;
    CHECK(lstat(absolute, &st) == 0 && S_ISLNK(st.st_mode), "lstat 看到 symlink 本身");
    CHECK(stat(absolute, &st) == 0 && S_ISREG(st.st_mode) && st.st_size == 7,
          "stat 跟随最终 symlink 到目标文件");

    fd = open(relative, O_RDONLY);
    CHECK(fd >= 0, "open 跟随相对 symlink 成功");
    char data[16];
    memset(data, 0, sizeof(data));
    CHECK(read(fd, data, 7) == 7 && strcmp(data, "link-ok") == 0,
          "通过相对 symlink 读到目标内容");
    close(fd);

    CHECK(mkdir(dir, 0) == 0, "创建中间路径测试目录成功");
    fd = open(inside, O_CREAT | O_RDWR);
    CHECK(fd >= 0, "创建中间路径目标文件成功");
    close(fd);
    CHECK(symlink(dir, alias) == 0, "创建目录 symlink 成功");
    CHECK(stat("/tmp/link_alias/inside.txt", &st) == 0 && S_ISREG(st.st_mode),
          "路径中间组件 symlink 被跟随");

    CHECK(symlink("/tmp/no_such_link_target", dangling) == 0, "允许创建 dangling symlink");
    CHECK(lstat(dangling, &st) == 0 && S_ISLNK(st.st_mode), "dangling symlink 可 lstat");
    CHECK(stat(dangling, &st) < 0, "dangling symlink 的 stat 失败");

    CHECK(link(target, hard) == 0, "创建 hard link 成功");
    CHECK(unlink(target) == 0, "删除 hard link 原名字成功");
    fd = open(hard, O_RDONLY);
    CHECK(fd >= 0, "删除原名字后 hard link 仍可打开");
    memset(data, 0, sizeof(data));
    CHECK(read(fd, data, 7) == 7 && strcmp(data, "link-ok") == 0,
          "hard link 共享同一文件内容");
    close(fd);

    unlink(absolute);
    unlink(relative);
    unlink(hard);
    unlink(dangling);
    unlink(alias);
    unlink(inside);
    rmdir(dir);

    TEST_END();
}
