#include "user.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        puts("usage: tick <cmd> [args...]\n");
        return 1;
    }

    long t0 = get_ticks();
    int pid = fork();
    if (pid == 0) {
        exec(argv[1], (const char **)&argv[1]);
        puts("[tick] exec failed\n");
        exit(127);
    }

    int status = 0;
    waitpid(pid, &status);    
    long t1 = get_ticks();

    puts("[tick] ");
    puts(argv[1]);
    puts(" = ");
    put_int((int)(t1 - t0));
    puts(" ticks, exit=");
    put_int(status);
    puts("\n");
    return 0;
}