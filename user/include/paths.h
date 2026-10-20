/*
 * paths.h —— 标准路径常量
 *
 * 只放"标准名字"：真正的设备节点现在还没有（/dev 是空目录，内核没有
 * /dev/null 这类字符设备），所以 open(_PATH_DEVNULL) 会 ENOENT。
 */
#ifndef RK_PATHS_H
#define RK_PATHS_H

#ifndef _PATH_DEVNULL
#define _PATH_DEVNULL   "/dev/null"
#endif
#ifndef _PATH_TTY
#define _PATH_TTY       "/dev/tty"
#endif
#ifndef _PATH_CONSOLE
#define _PATH_CONSOLE   "/dev/console"
#endif
#ifndef _PATH_BSHELL
#define _PATH_BSHELL    "/bin/sh"
#endif

#endif /* RK_PATHS_H */
