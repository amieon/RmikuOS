/* ============================================================================
 * sqlite_main.c —— RmikuOS 上 SQLite 的最小驱动 / 持久化探针
 *
 * 由 build.py 的 build_sqlite3() 专门编译，不经过 build_c_projects。
 * 唯一一处 #include "rmiku_vfs.h"（内含 sqlite3_os_init()）。
 *
 * 这个版本是「落盘探针」：
 *   1. 建表 + 插入 3 行（同连接内）
 *   2. 同连接内 SELECT 一次（验证缓存/事务层）
 *   3. sqlite3_close → sqlite3_open 重新打开 → SELECT 一次（验证真正落盘）
 *
 * 这一步「关掉再重开」是旧版没有的，也是判断 “数据到底有没有写进介质”
 * 的关键：如果重开后还能查到 3 行，说明 xWrite/xSync 链路是好的，
 * 旧版 in-run 0 行只是缓存/事务层面的假象；如果重开也是 0 行，
 * 才是写盘链路（VFS 或 FAT 驱动）真的丢数据。
 *
 * 数据库必须落在可写挂载点：
 *   /tmp  → tmpfs（内存，重启即失，仅用于单连接自测）
 *   /fat  → FAT 落盘（重启仍在，验证真实读写请用它）
 * ==========================================================================*/

#include "sqlite3.h"
#include "rmiku_vfs.h"
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

/* 返回 1=成功 0=失败（打印错误）。旧版忽略 PRAGMA 返回值，这里全部打印。 */
static int must(sqlite3 *db, const char *sql) {
    char *err = 0;
    int rc = sqlite3_exec(db, sql, 0, 0, &err);
    if (rc != SQLITE_OK) {
        printf("[FAIL] %s\n       -> rc=%d %s\n", sql, rc,
               err ? err : sqlite3_errmsg(db));
        if (err) sqlite3_free(err);
        return 0;
    }
    printf("[ok]  %s\n", sql);
    return 1;
}

/* 打印 journal_mode 当前值，确认 PRAGMA 真的生效了（在回调内直接打印，
 * 不保存 val 指针，避免 sqlite3_exec 返回后指针失效） */
static int capture_mode(void *ctx, int ncol, char **val, char **name) {
    (void)ctx; (void)ncol; (void)name;
    printf("journal_mode = %s\n", (val && val[0]) ? val[0] : "?");
    return 0;
}

/* 跑一条 SELECT 并报告行数；返回行数，失败返回 -1 */
static int run_select(sqlite3 *db, const char *sql) {
    int nrow = 0;
    char *err = 0;
    int rc = sqlite3_exec(db, sql, on_row, &nrow, &err);
    if (rc != SQLITE_OK) {
        printf("[FAIL] select: %s\n", err ? err : "?");
        if (err) sqlite3_free(err);
        return -1;
    }
    printf("   -> %d row(s)\n\n", nrow);
    return nrow;
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

    sqlite3_exec(db, "PRAGMA journal_mode;", capture_mode, 0, 0);
    printf("(default)\n\n");

    /* 单进程 + 无 WAL：日志走内存，绕开 journal 临时文件的删除/重命名语义。
     * 注意：即便用 MEMORY 日志，DB 主文件的数据页仍会在 commit 时写盘。 */
    must(db, "PRAGMA journal_mode=MEMORY;");
    must(db, "PRAGMA synchronous=OFF;");

    sqlite3_exec(db, "PRAGMA journal_mode;", capture_mode, 0, 0);
    printf("(after pragma)\n\n");

    if (!must(db, "DROP TABLE IF EXISTS t;"))                       goto done;
    if (!must(db, "CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT, score REAL);")) goto done;
    if (!must(db, "INSERT INTO t(name,score) VALUES('miku', 39.39);"))  goto done;
    if (!must(db, "INSERT INTO t(name,score) VALUES('rin',  1.0);"))    goto done;
    if (!must(db, "INSERT INTO t(name,score) VALUES('len',  2.0);"))    goto done;

    printf("[1] IN-RUN SELECT (读页缓存)\n");
    run_select(db, "SELECT id,name,score FROM t ORDER BY id;");

    /* ---- 真正的落盘验证：关闭连接，重新打开，从磁盘读 ---- */
    sqlite3_close(db);
    db = 0;
    printf("[2] REOPEN SELECT (从磁盘读 —— 这才是“落盘”的真相)\n");
    rc = sqlite3_open(path, &db);
    if (rc != SQLITE_OK) {
        printf("[FAIL] reopen: rc=%d %s\n", rc, db ? sqlite3_errmsg(db) : "?");
        return 1;
    }
    int n1 = run_select(db, "SELECT id,name,score FROM t ORDER BY id;");
    int n2 = run_select(db, "SELECT COUNT(*) AS n, SUM(score) AS total FROM t;");

    if (n1 == 3 && n2 == 1)
        printf("[PASS] 数据已落盘：重开后仍能查到 3 行。\n");
    else
        printf("[FAIL] 数据未落盘：重开后为 %d 行（期望 3）。\n", n1);

done:
    if (db) sqlite3_close(db);
    printf("done.\n");
    return 0;
}
