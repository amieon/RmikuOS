/*
 * user/dl/dlopen_plugin.c —— 被 mini loader 加载的最小 ET_DYN 插件。
 *
 * 这个目录不参与 collect_sources() 的扫描（它只 glob user/tests/*.c 等），
 * 所以这里可以放没有 main 的源码；产物是 build/<arch>/dl/dlopen_plugin.so，
 * 由 mkfs_ext4.sh 拷到 /usr/lib/。绝不能放进 /tests —— run_all 会 fork+exec
 * 目录里的每一个文件，一个 ET_DYN 进去就是一条假的 FAIL。
 *
 * 故意留三种重定位让 dlopen_test 能逐项验收：
 *   - __attribute__((constructor)) → .init_array 里的 R_*_RELATIVE
 *   - &g_local                    → .data 里的 R_*_RELATIVE
 *   - rk_host_add / rk_host_bump  → .rela.plt 里的 R_*_JUMP_SLOT
 * 后两个符号主程序用 rk_dl_register() 登记，loader 查全局表解析。
 */
#include "string.h"

extern int  rk_host_add(int a, int b);
extern void rk_host_bump(int v);

static int g_ctor_ran = 0;
static int g_dtor_ran = 0;
static int g_local    = 0x5a5a;

__attribute__((constructor))
static void plugin_ctor(void) {
    g_ctor_ran = 1;
    rk_host_bump(10);
}

__attribute__((destructor))
static void plugin_dtor(void) {
    g_dtor_ran = 1;
    rk_host_bump(20);
}

int plugin_add(int a, int b) {
    return a + b;
}

int plugin_call_host(int a, int b) {
    return rk_host_add(a, b);
}

int plugin_ctor_seen(void) {
    return g_ctor_ran;
}

int plugin_dtor_seen(void) {
    return g_dtor_ran;
}

int *plugin_data_addr(void) {
    return &g_local;
}

int plugin_data_value(void) {
    return g_local;
}

const char *plugin_name(void) {
    return "dlopen_plugin";
}
