#include "user.h"
static void test0(){
    int fd[2];
    int ret = pipe(fd);
    printf("pipe ret=%d fd[0]=%d fd[1]=%d\n", ret, fd[0], fd[1]);
}
static void test1(){
    int fd[2];
    pipe(fd);
    write(fd[1], "hello", 5);     
    char buf[16];
    int n = read(fd[0], buf, 16); 
    buf[n] = 0;
    printf("read %d bytes: %s\n", n, buf);
}
static void test2(){
    int fd[2];
    pipe(fd);
    int pid = fork();
    if (pid == 0) {
        char buf[16];
        int n = read(fd[0], buf, 16);   
        buf[n] = 0;
        printf("child read: %s\n", buf);
        exit(0);
    } else {
        write(fd[1], "hi", 2);         
        waitpid(pid, 0, 0);
    }
}
static void test3(){
    int fd[2];
    pipe(fd);
    int pid = fork();
    if (pid == 0) {
        close(fd[1]);               
        char buf[16];
        int n;
        while ((n = read(fd[0], buf, 16)) > 0) { 
            buf[n] = 0;
            printf("got: %s\n", buf);
        }
        printf("EOF, n=%d\n", n);       
        exit(0);
    } else {
        close(fd[0]);                   
        write(fd[1], "data", 4);
        close(fd[1]);                   
        waitpid(pid, 0, 0);
    }
}
int main(int argc, char **argv) {
    test0();
    printf("TEST0 PASS\n");
    test1();
    printf("TEST1 PASS\n");
    test2();
    printf("TEST2 PASS\n");
    test3();
    printf("TEST3 PASS\n");
    return 0;
}