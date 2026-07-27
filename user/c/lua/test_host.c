/* lua_main.c —— RmikuOS Lua 宿主（只依赖 malloc/free，无 realloc） */
#include <stddef.h>

/* 你的 syscall（按实际名字改） */
extern int sys_write(int fd, const void *buf, int n);
extern int sys_open(const char *path, int flags);
extern int sys_read(int fd, void *buf, int n);
extern void sys_exit(int code);
extern void *sys_malloc(int size);
extern void sys_free(void *p);

#include "lua.h"
#include "lauxlib.h"

/* ========== 内存分配器（纯 malloc/free，无 realloc） ========== */
static void *l_alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
    (void)ud;
    if (nsize == 0) {
        if (ptr) sys_free(ptr);
        return NULL;
    }
    if (ptr == NULL) {
        return sys_malloc(nsize);
    }
    /* realloc: 新分配 + 手动拷贝 + 释放旧块 */
    void *newp = sys_malloc(nsize);
    if (!newp) return NULL;
    size_t copy = osize < nsize ? osize : nsize;
    char *d = newp, *s = ptr;
    for (size_t i = 0; i < copy; i++) d[i] = s[i];
    sys_free(ptr);
    return newp;
}

/* ========== 脚本加载（替代 fopen） ========== */
#define MAX_SCRIPT (64 * 1024)
static char script_buf[MAX_SCRIPT];

static int load_script(const char *path, const char **buf, int *len) {
    int fd = sys_open(path, 0);  /* O_RDONLY */
    if (fd < 0) return -1;
    int total = 0, n;
    while ((n = sys_read(fd, script_buf + total, 1024)) > 0) {
        total += n;
        if (total >= MAX_SCRIPT - 1) break;
    }
    /* 如果你有 sys_close，取消下面注释： */
    /* extern int sys_close(int fd); sys_close(fd); */
    script_buf[total] = '\0';
    *buf = script_buf; *len = total;
    return 0;
}

/* ========== 暴露给 Lua 的函数 ========== */
static int l_print(lua_State *L) {
    int n = lua_gettop(L);
    for (int i = 1; i <= n; i++) {
        if (i > 1) sys_write(1, "\t", 1);
        size_t l; const char *s = luaL_tolstring(L, i, &l);
        sys_write(1, s, l);
    }
    sys_write(1, "\n", 1);
    return 0;
}

/* ========== 手动注册库（替代 linit.c） ========== */
extern int luaopen_base(lua_State *L);
extern int luaopen_table(lua_State *L);
extern int luaopen_string(lua_State *L);
extern int luaopen_math(lua_State *L);
extern int luaopen_utf8(lua_State *L);
extern int luaopen_coroutine(lua_State *L);

static void open_libs(lua_State *L) {
    luaL_requiref(L, "_G", luaopen_base, 1);       lua_pop(L, 1);
    luaL_requiref(L, "table", luaopen_table, 1);   lua_pop(L, 1);
    luaL_requiref(L, "string", luaopen_string, 1);  lua_pop(L, 1);
    luaL_requiref(L, "math", luaopen_math, 1);     lua_pop(L, 1);
    luaL_requiref(L, "utf8", luaopen_utf8, 1);     lua_pop(L, 1);
    luaL_requiref(L, "coroutine", luaopen_coroutine, 1); lua_pop(L, 1);
    lua_pushcfunction(L, l_print); lua_setglobal(L, "print");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        const char msg[] = "Usage: lua <script.lua>\n";
        sys_write(2, msg, sizeof(msg)-1);
        sys_exit(1);
    }

    const char *script; int len;
    if (load_script(argv[1], &script, &len) < 0) {
        const char msg[] = "Error: cannot open script\n";
        sys_write(2, msg, sizeof(msg)-1);
        sys_exit(1);
    }

    lua_State *L = lua_newstate(l_alloc, NULL);
    if (!L) { sys_exit(1); }
    open_libs(L);

    if (luaL_loadbuffer(L, script, len, argv[1]) != LUA_OK ||
        lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK) {
        size_t l; const char *e = lua_tolstring(L, -1, &l);
        sys_write(2, e, l); sys_write(2, "\n", 1);
        lua_close(L); sys_exit(1);
    }

    lua_close(L);
    return 0;
}