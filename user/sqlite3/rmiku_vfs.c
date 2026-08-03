/* ============================================================================
 * rmiku_vfs.c —— RmikuOS 自定义 SQLite VFS 的编译单元
 *
 * rmiku_vfs.h 里所有函数都是 static inline, 必须有一个 .c 实例化出
 * sqlite3_os_init()/sqlite3_os_end() 符号供链接。
 *
 * 单独成文件而不是并进 rmiku_shims.c 的原因: rmiku_vfs.h 依赖 fs.h 的
 * 内核版 struct stat / struct dirent, 而 rmiku_shims.c 需要 POSIX 版
 * （sys/stat.h / dirent.h）, 两者同 TU 必然 struct 重定义冲突。
 * ==========================================================================*/

#include "rmiku_vfs.h"
