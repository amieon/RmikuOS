#include "user.h"
int main() {
    int fd = open_create("/tmp/note",O_RDWR);      
    write(fd, "hello tmpfs\n", 12); 
    close(fd);

    int fd2 = open_create("/tmp/note",O_RDWR);
    char buf[64];
    int n = read(fd2, buf, sizeof(buf)); 
    write(1, buf, n);                 
    close(fd2);
    return 0;
}