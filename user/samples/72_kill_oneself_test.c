#include "user.h"

int main() {
    printf("pid=%d, killing myself with SIGKILL\n", getpid());
    kill(getpid(), SIGKILL); 
    printf("should not reach here\n");
    return 0;
}