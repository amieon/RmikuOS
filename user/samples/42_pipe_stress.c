#include "user.h"
static void test0(){
    int fd[2];
    pipe(fd);
    int pid = fork();
    if (pid == 0) {
        close(fd[1]);
        char buf[64];
        int total = 0, n;
        while ((n = read(fd[0], buf, 64)) > 0) {
            total += n;
            for (int i = 0; i < 10; i++) yield();
        }
        uprintf("child total read = %d\n", total);   // 应该 = 4096
        exit(0);
    } else {
        close(fd[0]);
        char data[4096];
        for (int i = 0; i < 4096; i++) data[i] = 'A' + (i % 26);
        int total = 0, n;
        while (total < 4096) {
            n = write(fd[1], data + total, 4096 - total);
            if (n < 0) break;
            total += n;
        }
        uprintf("parent total written = %d\n", total);  // 应该 = 4096
        close(fd[1]);
        waitpid(pid, 0, 0);
    }
}
static void test1(){
    int fd[2];
    pipe(fd);
    int pid = fork();
    if (pid == 0) {
        close(fd[1]);
        char buf[64];
        int total = 0, n;
        int expect = 0;
        while ((n = read(fd[0], buf, 64)) > 0) {
            for (int i = 0; i < n; i++) {
                if ((unsigned char)buf[i] != (expect % 256)) {
                    uprintf("MISMATCH at %d: got %d expect %d\n", expect, buf[i], expect % 256);
                    exit(1);
                }
                expect++;
            }
        }
        uprintf("verified %d bytes, all correct\n", expect);
        exit(0);
    } else {
        close(fd[0]);
        char data[4096];
        for (int i = 0; i < 4096; i++) data[i] = (i % 256);
        int total = 0, n;
        while (total < 4096) {
            n = write(fd[1], data + total, 4096 - total);
            if (n < 0) break;
            total += n;
        }
        uprintf("parent total written = %d\n", total);  // 应该 = 4096
        close(fd[1]);
        waitpid(pid, 0, 0);
    }
}
static void test2(){
    int fd[2];
    pipe(fd);
    for (int k = 0; k < 3; k++) {
        if (fork() == 0) {
            // 子进程:写自己的编号 100 次
            char c = '0' + k;
            for (int i = 0; i < 100; i++) write(fd[1], &c, 1);
            close(fd[1]);
            exit(0);
        }
    }
    close(fd[1]);
    char receive[300];
    read(fd[0],receive,300);
    uprintf("Get 300 chars\n");
}
static void test3(){
    int fd[2];
    pipe(fd);
    int pid = fork();
    if (pid == 0) {
        exec("directly_return");
        uprintf("exec failed!\n");
        exit(1);
    } else {
        close(fd[1]);
        char buf[16];
        int n = read(fd[0], buf, 16); 
        uprintf("parent read n=%d (expect 0 = EOF)\n", n);
        waitpid(pid, 0, 0);
    }
}
static void test4(){
    for (int i = 0; i < 200; i++) {
        int fd[2];
        pipe(fd);
        int pid = fork();
        if (pid == 0) {
            close(fd[1]);
            char buf[16];
            read(fd[0], buf, 16);
            close(fd[0]);
            exit(0);
        } else {
            close(fd[0]);
            write(fd[1], "x", 1);
            close(fd[1]);
            waitpid(pid, 0, 0);
        }
        uprintf("cycle %d done.\n",i);
    }
    uprintf("200 pipe cycles done\n");
}
int main(int argc, char **argv) {
    test0();
    uprintf("TEST0 PASS\n");
    test1();
    uprintf("TEST1 PASS\n");
    test2();
    uprintf("TEST2 PASS\n");
    test3();
    uprintf("TEST3 PASS\n");
    test4();
    uprintf("TEST4 PASS\n");
    return 0;
}