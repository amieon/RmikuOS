#include "user.h"

int main(void) {
    // 往 stdout(fd 1)写。如果被管道重定向了,这些就流进管道。
    const char *msg = "hello from writer\n";
    int len = 0;
    while (msg[len]) len++;          // strlen

    write(1, msg, len);

    // 再写几行,测试多次 write
    write(1, "line2\n", 6);
    write(1, "line3\n", 6);

    return 0;
}