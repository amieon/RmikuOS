#include "test.h"
#include "../sqlite3/sqlite3.h"

/* ============================================================================
 * sqlite_test.c —— RmikuOS SQLite 落盘回归测试(规范化断言版)
 *
 * 由 build.py 的 build_sqlite3() 专门编译(与 sqlite_main.c 同路径),
 * 产物为 /programs/sqlite_test。
 *
 * 逻辑(继承 sqlite_main.c 的"重开验证"思想):
 *   1. 建表 + 插入 3 行(同连接内)
 *   2. 同连接 SELECT 一次(验证缓存/事务层)
 *   3. sqlite3_close → sqlite3_open 重新打开 → SELECT 一次(验证真正落盘)
 *
 * 数据库落在 /fat(FAT 落盘,验证 xWrite/xSync 链路);若 /fat 未挂载,
 * sqlite3_open 会失败,测试会明确 FAIL。
 * ==========================================================================*/

#ifndef DB_PATH
#define DB_PATH "/fat/sqlite_test.db"
#endif

static int on_row(void *ctx, int ncol, char **val, char **name) {
    (void)val; (void)name; (void)ncol;
    int *nrow = (int *)ctx;
    (*nrow)++;
    return 0;
}

int main() {
    TEST_START("sqlite3");

    unlink(DB_PATH);   /* 清场 */

    sqlite3 *db = 0;
    char *err = 0;
    int rc;

    /* 1. 打开 + 建表 + 插入 */
    rc = sqlite3_open(DB_PATH, &db);
    CHECK(rc == SQLITE_OK, "sqlite3_open 成功(/fat)");

    rc = sqlite3_exec(db,
        "CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT);", 0, 0, &err);
    CHECK(rc == SQLITE_OK, "CREATE TABLE 成功");

    rc = sqlite3_exec(db,
        "INSERT INTO t (name) VALUES ('a');"
        "INSERT INTO t (name) VALUES ('b');"
        "INSERT INTO t (name) VALUES ('c');", 0, 0, &err);
    CHECK(rc == SQLITE_OK, "插入 3 行成功");

    int nrow = 0;
    rc = sqlite3_exec(db, "SELECT * FROM t;", on_row, &nrow, &err);
    CHECK(rc == SQLITE_OK, "同连接 SELECT 成功");
    CHECK_EQ(nrow, 3, "同连接查询到 3 行");

    sqlite3_close(db);

    /* 2. 重开:验证真正落盘 */
    db = 0;
    rc = sqlite3_open(DB_PATH, &db);
    CHECK(rc == SQLITE_OK, "重开 sqlite3_open 成功");
    nrow = 0;
    rc = sqlite3_exec(db, "SELECT * FROM t;", on_row, &nrow, &err);
    CHECK(rc == SQLITE_OK, "重开后 SELECT 成功");
    CHECK_EQ(nrow, 3, "重开后仍查到 3 行(真实落盘)");
    sqlite3_close(db);

    unlink(DB_PATH);
    TEST_END();
}
