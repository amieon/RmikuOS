/* mmap/munmap/mprotect 与 PROT_* 实现在 mem.h（3 参数匿名映射） */
#include "mem.h"

/* MAP_* 仅占位供编译：RmikuOS 教学版不做文件映射/固定地址 */
#define MAP_SHARED 0x01
#define MAP_PRIVATE 0x02
#define MAP_FIXED 0x10
#define MAP_ANON 0x20
#define MAP_FAILED ((void *)-1)
