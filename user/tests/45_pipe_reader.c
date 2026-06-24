#include "user.h"

int main(void) {
    char buf[64];

    // 从 stdin(fd 0)读,直到 EOF(read 返回 0)。
    // 如果被管道重定向了,这里读的就是管道数据。
    while (1) {
        isize n = read(0, buf, sizeof(buf));

        if (n < 0) {
            // 读错误
            const char *err = "reader: read error\n";
            int l = 0; while (err[l]) l++;
            write(1, err, l);
            return 1;
        }

        if (n == 0) {
            // EOF:写端全关了,正常结束
            break;
        }

        // 把读到的原样写到 stdout(屏幕)
        write(1, buf, n);
    }

    // 标记一下 reader 正常收到 EOF 退出了
    const char *done = "[reader got EOF]\n";
    int l = 0; while (done[l]) l++;
    write(1, done, l);

    return 0;
}