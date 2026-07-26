#ifndef SETJMP_H
#define SETJMP_H
typedef struct { unsigned long buf[14]; } jmp_buf[1];
int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);
#endif