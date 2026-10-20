/*
 * strings.h —— POSIX 名字，实现归在 string.h
 *
 * 为什么单独存在一个只有 include 的头：
 *   POSIX 把 strcasecmp/strncasecmp 放在 <strings.h>，而 C 标准把
 *   strcmp 那一族放在 <string.h>。移植代码（git 的 41 个文件）写的是
 *   #include <strings.h>，我们不可能去改它的源码。
 *
 * 实现只有一份（在 string.h 里），这里是纯转发，不做二次定义。
 */
#ifndef RK_STRINGS_H
#define RK_STRINGS_H

#include "string.h"

#endif /* RK_STRINGS_H */
