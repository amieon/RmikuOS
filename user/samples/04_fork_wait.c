#include "user.h"

int main(void) {
    puts("[fork_wait] parent before fork, pid=");
    printf("%d", getpid());
    puts("\n");

    isize child = fork();

    if (child == 0) {
        puts("[fork_wait] child running, pid=");
        printf("%d", getpid());
        puts("\n");

        puts("[fork_wait] child sleep 3 ticks\n");
        sleep(3);

        puts("[fork_wait] child exit 42\n");
        exit(42);
    } else if (child > 0) {
        puts("[fork_wait] parent forked child=");
        printf("%d", child);
        puts("\n");

        int code = -1;
        isize ret = waitpid(child, &code, 0);

        puts("[fork_wait] parent waitpid ret=");
        printf("%d", ret);
        puts(", code=");
        printf("%d", code);
        puts("\n");

        exit(0);
    } else {
        puts("[fork_wait] fork failed\n");
        exit(1);
    }
}