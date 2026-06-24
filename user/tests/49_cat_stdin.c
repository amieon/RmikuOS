#include "user.h"
int main(void) {
    char buf[64];
    isize n;
    while ((n = read(0, buf, sizeof(buf))) > 0) {
        write(1, buf, n);
    }
    return 0;
}