/*
 * user/tests/dlopen_test.c —— mini loader 的真机回归。
 *
 * 验收的不只是「能 dlopen」：四步里每一步都有专门的探针
 *   映射/重定位  : plugin_data_value() == 0x5a5a（RELATIVE 填对了数据指针）
 *   符号解析     : plugin_call_host() 打到主程序的 rk_host_add（JUMP_SLOT）
 *   构造/析构    : constructor/destructor 通过 host_bump 留痕
 *   句柄/错误    : 引用计数、LOCAL→GLOBAL、dlerror、负向路径
 * 任何一步退化成「返回个 NULL 但不报错」都会红，不会静默混进 PASS。
 */
#include "test.h"
#include "rk_dlfcn.h"

#define RK_DL_FN(f) ((void *)(unsigned long long)(void (*)(void))(f))
#define SO "/usr/lib/dlopen_plugin.so"

typedef int         (*fn0_t)(void);
typedef int         (*fn2_t)(int, int);
typedef int *       (*fnp_t)(void);
typedef const char *(*fns_t)(void);

static int g_bumps[8];
static int g_nbump;
static int g_add_calls;

static int host_add(int a, int b) {
    g_add_calls++;
    return a + b;
}

static void host_bump(int v) {
    if (g_nbump < 8) g_bumps[g_nbump++] = v;
}

int main(void) {
    TEST_START("dlopen");

    CHECK_EQ(rk_dl_register("rk_host_add",  RK_DL_FN(host_add)), 0,
             "注册主程序符号 rk_host_add");
    CHECK_EQ(rk_dl_register("rk_host_bump", RK_DL_FN(host_bump)), 0,
             "注册主程序符号 rk_host_bump");

    /* ---- 负向：文件不存在 / 不是 ELF ---- */
    void *bad = dlopen("/usr/lib/no_such_plugin.so", RTLD_NOW);
    CHECK(bad == 0, "不存在的 .so 返回 NULL");
    const char *e = dlerror();
    CHECK(e != 0, "失败后 dlerror 非空");
    CHECK(e && strstr(e, "cannot read") != 0, "dlerror 报的是读文件失败");
    CHECK(dlerror() == 0, "dlerror 消费后清空");

    CHECK(dlopen("/etc/motd", RTLD_NOW) == 0, "非 ELF 文件被拒绝");
    e = dlerror();
    CHECK(e && strstr(e, "ELF64") != 0, "dlerror 明确说不是 ELF64");

    /* ---- 正向：加载 ---- */
    void *h = dlopen(SO, RTLD_NOW | RTLD_LOCAL);
    CHECK(h != 0, "dlopen 插件成功");
    if (!h) { TEST_END(); }

    CHECK_EQ(g_nbump, 1, ".init_array 构造函数跑了一次");
    CHECK_EQ(g_bumps[0], 10, "构造函数确实调到了主程序的 host_bump");

    fn0_t ctor_seen = (fn0_t)dlsym(h, "plugin_ctor_seen");
    CHECK(ctor_seen != 0, "dlsym 找到 plugin_ctor_seen");
    CHECK(ctor_seen && ctor_seen() == 1, "插件内的静态状态已初始化");

    fns_t pname = (fns_t)dlsym(h, "plugin_name");
    CHECK(pname != 0, "dlsym 找到 plugin_name");
    CHECK_STREQ(pname ? pname() : "(null)", "dlopen_plugin",
                "插件导出的字符串可读");

    fn2_t padd = (fn2_t)dlsym(h, "plugin_add");
    CHECK(padd != 0, "dlsym 找到 plugin_add");
    CHECK(padd && padd(3, 4) == 7, "插件代码真的能执行");

    /* RELATIVE 重定位：静态数据的地址必须被填成 load_bias + 原 vaddr */
    fnp_t pdata = (fnp_t)dlsym(h, "plugin_data_addr");
    CHECK(pdata != 0, "dlsym 找到 plugin_data_addr");
    CHECK(pdata && pdata() != 0, "静态数据指针非 NULL");
    CHECK(pdata && *pdata() == 0x5a5a, "RELATIVE 重定位填对了数据地址");
    fn0_t pval = (fn0_t)dlsym(h, "plugin_data_value");
    CHECK(pval && pval() == 0x5a5a, "插件读自己的静态数据正确");

    /* JUMP_SLOT 重定位：PLT 项要解析到主程序，而不是跳进 0 */
    CHECK_EQ(g_add_calls, 0, "调用前 host_add 未被调用");
    fn2_t pcall = (fn2_t)dlsym(h, "plugin_call_host");
    CHECK(pcall != 0, "dlsym 找到 plugin_call_host");
    CHECK(pcall && pcall(17, 23) == 40, "JUMP_SLOT 解析到主程序函数");
    CHECK_EQ(g_add_calls, 1, "主程序 host_add 被真的调用了一次");

    /* dlsym 负向 + dlerror 双缓冲 */
    CHECK(dlsym(h, "no_such_symbol") == 0, "查不到的符号返回 NULL");
    e = dlerror();
    CHECK(e && strstr(e, "not found") != 0, "dlerror 报符号未找到");
    CHECK(dlerror() == 0, "dlerror 第二次返回 NULL");

    /* RTLD_LOCAL 不进全局作用域；重复 dlopen 复用句柄并升级为 GLOBAL */
    CHECK(dlsym(RTLD_DEFAULT, "plugin_add") == 0,
          "RTLD_LOCAL 的符号不进全局作用域");
    (void)dlerror();
    void *h2 = dlopen(SO, RTLD_NOW | RTLD_GLOBAL);
    CHECK(h2 == h, "同一路径重复 dlopen 复用同一句柄(引用计数)");
    CHECK(dlsym(RTLD_DEFAULT, "plugin_add") != 0,
          "RTLD_GLOBAL 升级后符号全局可见");

    /* 全局表里的内置 libc 符号也要能查到 */
    CHECK(dlsym(RTLD_DEFAULT, "strlen") != 0,
          "dlsym(RTLD_DEFAULT) 能查到内置 libc 符号");

    /* ---- 卸载：引用计数 2 → 1 → 0 ---- */
    fn0_t dtor_seen = (fn0_t)dlsym(h, "plugin_dtor_seen");
    CHECK_EQ(dlclose(h), 0, "第一次 dlclose 成功");
    CHECK(dtor_seen && dtor_seen() == 0, "引用计数未归零时不跑析构");
    CHECK_EQ(g_nbump, 1, "第一次 dlclose 没有触发析构");

    CHECK_EQ(dlclose(h), 0, "第二次 dlclose 成功");
    CHECK_EQ(g_nbump, 2, "析构函数跑了一次");
    CHECK_EQ(g_bumps[1], 20, "析构确实调到了主程序的 host_bump");

    CHECK_EQ(dlclose(h), -1, "已释放的句柄再 dlclose 返回 -1");
    e = dlerror();
    CHECK(e && strstr(e, "invalid handle") != 0, "dlerror 报无效句柄");

    TEST_END();
}
