/*
 * paths.h —— 标准路径常量
 *
 * 只放"标准名字"。设备节点不在文件系统里：/dev 是个空目录，但
 * /dev/null 和 /dev/urandom 由内核的内置设备表直接接管
 * （kernel/src/fs/null.rs 的 builtin_device，在挂载表和 O_CREAT 之前拦下），
 * open 这两个路径是成功的。其余（/dev/zero、/dev/tty、/dev/console）
 * 还没有实现，open 会 ENOENT —— 宁可报错，也不返回行为不对的东西。
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
