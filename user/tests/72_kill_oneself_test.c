#include "user.h"

int main() {
    uprintf("pid=%d, killing myself with SIGKILL\n", getpid());
    kill(getpid(), SIGKILL); 
    uprintf("should not reach here\n");
    return 0;
}