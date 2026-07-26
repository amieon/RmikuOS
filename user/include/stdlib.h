#ifndef STDLIB_H
#define STDLIB_H
#include <stddef.h>
#include "malloc.h"
#include "io.h"
double strtod(const char *s, char **e);
long strtol(const char *s, char **e, int b);
unsigned long strtoul(const char *s, char **e, int b);
int rand(void);
void srand(unsigned int s);

void abort(void){
    printf("abort\n");
    exit(127);
}

char *getenv(const char *n);

void *realloc(void *p, size_t s);
void qsort(void *b, size_t n, size_t s, int (*c)(const void *, const void *));
#endif