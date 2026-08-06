#include "test.h"

/* 管道:4KB 数据交替写读,内容校验(避免管道满阻塞) */
int main() {
    TEST_START("pipe_stress");

    int fd[2];
    CHECK(pipe(fd) == 0, "pipe 创建成功");

    char out[512];
    for (int i = 0; i < 512; i++) out[i] = (char)(i & 0xff);

    char in[512];
    int ok = 1;
    int total = 0;

    /* 交替写 512 / 读 512,管道不积压,单进程即可 */
    for (int round = 0; round < 8; round++) {
        isize w = write(fd[1], out, 512);
        if (w != 512) { ok = 0; break; }
        isize r = read(fd[0], in, 512);
        if (r != 512) { ok = 0; break; }
        for (int i = 0; i < 512; i++) {
            if (in[i] != out[i]) { ok = 0; break; }
        }
        total += (int)r;
    }
    CHECK_EQ(total, 4096, "累计读写 4096 字节");
    CHECK(ok, "管道数据逐字节一致");

    close(fd[0]);
    close(fd[1]);
    TEST_END();
}
