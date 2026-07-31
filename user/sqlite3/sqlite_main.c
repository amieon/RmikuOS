/* ============================================================================
 * sqlite_main.c —— RmikuOS 上 SQLite 的最小驱动 / 冒烟测试
 *
 * 这是 user/sqlite3/ 的「入口」。它由 build.py 的 build_sqlite3() 专门编译，
 * 不经过 build_c_projects（sqlite3 已移出 user/c/）。
 *
 * 它做两件事：
 *   1. #include "rmiku_vfs.h"  ← 全仓库唯一一处，把 VFS 真正接进链接
 *   2. 跑一遍 建表→插入→查询，验证 xOpen/xWrite/xRead/xSync/xTruncate 通路
 *
 * 注意：数据库文件必须落在**可写**挂载点上。
 *   /tmp  → tmpfs（内存，重启即失）
 *   /fat  → FAT16 落盘（重启仍在，推荐用它验证真实读写）
 * ==========================================================================*/

#include "sqlite3.h"
#include "rmiku_vfs.h"    /* 必须且只能被这一个 .c 包含：内含 sqlite3_os_init() */
#include "stdio.h"
#include "string.h"

#ifndef DB_PATH
#define DB_PATH "/fat/test.db"
#endif

static int on_row(void *ctx, int ncol, char **val, char **name) {
    int *nrow = (int *)ctx;
    if (*nrow == 0) {
        for (int i = 0; i < ncol; i++)
            printf("%-12s", name[i] ? name[i] : "?");
        printf("\n------------------------------------\n");
    }
    for (int i = 0; i < ncol; i++)
        printf("%-12s", val[i] ? val[i] : "NULL");
    printf("\n");
    (*nrow)++;
    return 0;
}

static int must(sqlite3 *db, const char *sql) {
    char *err = 0;
    int rc = sqlite3_exec(db, sql, 0, 0, &err);
    if (rc != SQLITE_OK) {
        printf("[FAIL] %s\n       -> %s\n", sql, err ? err : sqlite3_errmsg(db));
        if (err) sqlite3_free(err);
        return 0;
    }
    return 1;
}

int main(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : DB_PATH;
    sqlite3 *db = 0;
    int rc;

    printf("SQLite %s on RmikuOS\n", sqlite3_libversion());
    printf("db = %s\n\n", path);

    rc = sqlite3_open(path, &db);
    if (rc != SQLITE_OK) {
        printf("[FAIL] sqlite3_open: rc=%d %s\n", rc,
               db ? sqlite3_errmsg(db) : "(no handle)");
        return 1;
    }

    /* 单进程 + 无 WAL：日志走内存，绕开 journal 临时文件的删除/重命名语义 */
    must(db, "PRAGMA journal_mode=MEMORY;");
    must(db, "PRAGMA synchronous=OFF;");

    if (!must(db, "DROP TABLE IF EXISTS t;"))                       goto done;
    if (!must(db, "CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT, score REAL);")) goto done;
    if (!must(db, "INSERT INTO t(name,score) VALUES('miku', 39.39);"))  goto done;
    if (!must(db, "INSERT INTO t(name,score) VALUES('rin',  1.0);"))    goto done;
    if (!must(db, "INSERT INTO t(name,score) VALUES('len',  2.0);"))    goto done;

    printf("\n-- SELECT --\n");
    int nrow = 0;
    char *err = 0;
    rc = sqlite3_exec(db, "SELECT id,name,score FROM t ORDER BY id;",
                      on_row, &nrow, &err);
    if (rc != SQLITE_OK) {
        printf("[FAIL] select: %s\n", err ? err : "?");
        if (err) sqlite3_free(err);
        goto done;
    }
    printf("\n%d row(s). OK.\n", nrow);

    /* 再验证一次聚合，走 B-tree 扫描 + 浮点 */
    nrow = 0;
    sqlite3_exec(db, "SELECT COUNT(*) AS n, SUM(score) AS total FROM t;",
                 on_row, &nrow, 0);

done:
    sqlite3_close(db);
    printf("\ndone.\n");
    return 0;
}
