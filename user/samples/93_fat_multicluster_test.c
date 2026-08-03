#include "user.h"

// 测试 FAT 上的「多簇 + 按页偏移随机写 + 重开读回」——这正是 SQLite 的落盘模式。
// 88~92 全是用 <1 簇的小文件顺序写，没覆盖这种场景：
//   SQLite 把 db 当成一串 4096 字节页，按页号偏移 lseek+write，再按偏移 read 回来。
// 本测试写 8 页（32768 字节），每页填充可识别的模式，fsync、关闭、重开，逐页读回校验。
// 若通过，说明 FAT 多簇随机偏移读写没问题，SQLite 的“0 行”应往 VFS/事务层查；
// 若失败，说明 FAT 驱动在多簇写/读上有 bug（88~92 没测到）。

#define NPAGES 8
#define PSIZE  4096

static int pass_count = 0;
static int fail_count = 0;

static void expect_ok(const char *what, isize ret) {
    if (ret >= 0) { puts("  PASS: "); puts(what); puts("\n"); pass_count++; }
    else { puts("  FAIL: "); puts(what); puts(" (got "); printf("%d", (int)ret); puts(")\n"); fail_count++; }
}
static void expect_eq(const char *what, isize got, isize want) {
    if (got == want) { puts("  PASS: "); puts(what); puts("\n"); pass_count++; }
    else { puts("  FAIL: "); puts(what); puts(" (want "); printf("%d", (int)want);
           puts(", got "); printf("%d", (int)got); puts(")\n"); fail_count++; }
}

/* 第 p 页填充：每字节 = (p*7 + i) & 0xFF，简单可识别模式 */
static void fill_page(unsigned char *buf, int p) {
    for (int i = 0; i < PSIZE; i++) buf[i] = (unsigned char)((p * 7 + i) & 0xFF);
}
static int page_ok(const unsigned char *buf, int p) {
    for (int i = 0; i < PSIZE; i++)
        if (buf[i] != (unsigned char)((p * 7 + i) & 0xFF)) return 0;
    return 1;
}

static int path_exists(const char *path) { struct stat st; return stat(path, &st) >= 0; }
static isize file_size(const char *path) { struct stat st; if (stat(path, &st) < 0) return -1; return (isize)st.st_size; }

int main(void) {
    puts("=== fat multicluster (sqlite-style) test ===\n");

    if (!path_exists("/fat")) {
        puts("/fat not mounted, skip\n");
        puts("PASS: "); printf("%d", pass_count); puts("\n");
        puts("FAIL: "); printf("%d", fail_count); puts("\n");
        return fail_count == 0 ? 0 : 1;
    }

    const char *path = "/fat/mc_f.bin";
    unsigned char wbuf[PSIZE], rbuf[PSIZE];

    puts("\n[1] write 8 pages at sequential page offsets (like SQLite xWrite)\n");
    isize fd = open_create(path, O_RDWR);
    expect_ok("create /fat/mc_f.bin", fd);
    if (fd >= 0) {
        int allok = 1;
        for (int p = 0; p < NPAGES; p++) {
            fill_page(wbuf, p);
            lseek(fd, (isize)(p * PSIZE), 0);           // SEEK_SET
            isize n = write(fd, wbuf, PSIZE);
            if (n != PSIZE) { allok = 0; break; }
        }
        expect_eq("all 8 pages written", allok ? (isize)NPAGES : -1, (isize)NPAGES);
        expect_eq("fsync == 0", fsync(fd), 0);
        expect_eq("size == 32768 after write", file_size(path), (isize)(NPAGES * PSIZE));
        close(fd);
    }

    puts("\n[2] reopen and read each page back at its offset (like SQLite xRead)\n");
    fd = open(path, O_RDWR);
    expect_ok("reopen /fat/mc_f.bin", fd);
    if (fd >= 0) {
        int allok = 1;
        for (int p = 0; p < NPAGES; p++) {
            lseek(fd, (isize)(p * PSIZE), 0);
            isize n = read(fd, rbuf, PSIZE);
            if (n != PSIZE || !page_ok(rbuf, p)) { allok = 0; break; }
        }
        expect_eq("all 8 pages read back correctly", allok ? (isize)NPAGES : -1, (isize)NPAGES);
        close(fd);
    }

    puts("\n[3] random-access read: page 5, then page 1, then page 7 (non-sequential)\n");
    fd = open(path, O_RDWR);
    expect_ok("reopen for random read", fd);
    if (fd >= 0) {
        int ok = 1;
        int order[] = {5, 1, 7, 3, 0, 6, 2, 4};
        for (int k = 0; k < NPAGES; k++) {
            int p = order[k];
            lseek(fd, (isize)(p * PSIZE), 0);
            if (read(fd, rbuf, PSIZE) != PSIZE || !page_ok(rbuf, p)) { ok = 0; break; }
        }
        expect_eq("non-sequential reads all correct", ok ? (isize)NPAGES : -1, (isize)NPAGES);
        close(fd);
    }

    expect_ok("cleanup /fat/mc_f.bin", unlink(path));

    puts("\n=== summary ===\n");
    puts("PASS: "); printf("%d", pass_count); puts("\n");
    puts("FAIL: "); printf("%d", fail_count); puts("\n");
    if (fail_count == 0) { puts("ALL TESTS PASSED\n"); return 0; }
    puts("SOME TESTS FAILED\n");
    return 1;
}
