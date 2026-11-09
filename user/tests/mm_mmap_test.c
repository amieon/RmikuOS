#include "test.h"

/* 虚拟内存：六参 mmap / 匿名映射 / eager 文件映射 / MAP_FIXED 覆盖 / 写回 */
int main() {
    TEST_START("mm_mmap");

    /* ---- 1. 匿名映射：内部快捷方式 ---- */
    char *p = (char *)rk_anon_mmap(8192, PROT_READ | PROT_WRITE);
    CHECK(p != MAP_FAILED, "匿名映射 8KB(2 页)成功");
    if (p != MAP_FAILED) {
        p[0] = 'R'; p[1] = 'm'; p[2] = 'i'; p[3] = 'k'; p[4] = 'u'; p[5] = 0;
        CHECK_STREQ(p, "Rmiku", "匿名映射可写可读");
        p[4096] = 'X';
        CHECK(p[4096] == 'X', "多页映射:第 2 页可读写");
        p[8191] = 'Z';
        CHECK(p[8191] == 'Z', "映射末尾字节可读写");
        CHECK(munmap(p, 8192) == 0, "munmap 释放成功");
    }

    /* ---- 2. 标准六参匿名映射 ---- */
    char *q = (char *)mmap(0, 4096, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(q != MAP_FAILED, "六参 mmap 匿名映射成功");
    if (q != MAP_FAILED) {
        q[0] = 'A';
        CHECK(q[0] == 'A' && q[1] == 0, "新匿名页内容已清零");
        CHECK(munmap(q, 4096) == 0, "六参映射 munmap 成功");
    }

    /* ---- 3. MAP_FIXED：预留两块，覆盖后半段，前半段数据必须还在 ---- */
    char *base = (char *)rk_anon_mmap(8192, PROT_READ | PROT_WRITE);
    CHECK(base != MAP_FAILED, "预留 2 页成功");
    if (base != MAP_FAILED) {
        base[0] = 'L';
        base[4096] = 'R';

        char *half = (char *)mmap((void *)(base + 4096), 4096,
                                  PROT_READ | PROT_WRITE,
                                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        CHECK(half == base + 4096, "MAP_FIXED 返回指定地址");
        CHECK(base[0] == 'L', "覆盖后半段不影响前半段数据");
        CHECK(half != MAP_FAILED && half[0] == 0, "被覆盖的页是新清零页");
        if (half != MAP_FAILED) half[0] = 'W';
        CHECK(half != MAP_FAILED && half[0] == 'W', "MAP_FIXED 新页可写");

        /* NOREPLACE 撞已有映射必须 EEXIST，且不能破坏原内容 */
        errno = 0;
        void *nr = mmap((void *)base, 4096, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        CHECK(nr == MAP_FAILED && errno == EEXIST,
              "MAP_FIXED_NOREPLACE 重叠返回 EEXIST");
        CHECK(base[0] == 'L', "NOREPLACE 失败后原映射未被改动");

        /* 固定地址是命令：非页对齐 / 区间外一律 EINVAL */
        errno = 0;
        void *badaddr = mmap((void *)(base + 1), 4096, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        CHECK(badaddr == MAP_FAILED && errno == EINVAL,
              "MAP_FIXED 非页对齐 -> EINVAL");
        errno = 0;
        void *oob = mmap((void *)0x1000, 4096, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        CHECK(oob == MAP_FAILED && errno == EINVAL,
              "MAP_FIXED 区间外 -> EINVAL");

        /* 拆分后是两条独立 VMA，munmap 尚不支持跨段，分别释放。 */
        CHECK(munmap(base, 4096) == 0, "拆出的左段可单独 munmap");
        CHECK(munmap(base + 4096, 4096) == 0, "拆出的右段可单独 munmap");
    }

    /* ---- 4. eager 文件映射 ---- */
    char path[] = "/tmp/mmtestXXXXXX";
    int fd = (int)mkstemp(path);
    CHECK(fd >= 0, "mkstemp 建临时文件");
    if (fd >= 0) {
        char buf[5000];
        for (int i = 0; i < 5000; i++) buf[i] = (char)('a' + (i % 26));
        CHECK(write(fd, buf, 5000) == 5000, "写入 5000 字节(跨页)");

        char *m = (char *)mmap(0, 5000, PROT_READ, MAP_PRIVATE, fd, 0);
        CHECK(m != MAP_FAILED, "eager 文件映射成功");
        if (m != MAP_FAILED) {
            CHECK(m[0] == 'a' && m[1] == 'b', "映射首字节来自文件");
            CHECK(m[4096] == buf[4096], "跨页偏移 4096 内容正确");
            CHECK(m[4999] == buf[4999], "映射末字节内容正确");

            /* VMA 持有 FileRef，fd 只是凭据：关掉后映射仍可读 */
            close(fd);
            CHECK(m[0] == 'a' && m[4096] == buf[4096],
                  "close(fd) 后文件映射仍可读");
            CHECK(munmap(m, 5000) == 0, "文件映射 munmap 成功");
            fd = -1;
        }

        int fd2 = (int)open(path, O_RDWR);
        CHECK(fd2 >= 0, "重新以 O_RDWR 打开测试文件");
        if (fd2 >= 0) {
            /* offset 必须页对齐 */
            errno = 0;
            void *bad = mmap(0, 100, PROT_READ, MAP_PRIVATE, fd2, 1);
            CHECK(bad == MAP_FAILED && errno == EINVAL,
                  "offset 非页对齐 -> EINVAL");

            /* offset=4096：映射内容应从文件第 4096 字节开始 */
            char *o = (char *)mmap(0, 1000, PROT_READ, MAP_PRIVATE, fd2, 4096);
            CHECK(o != MAP_FAILED, "offset=4096 文件映射成功");
            if (o != MAP_FAILED) {
                CHECK(o[0] == buf[4096] && o[903] == buf[4999],
                      "offset=4096 内容对应文件偏移");
                CHECK(munmap(o, 1000) == 0, "offset 映射 munmap 成功");
            }

            /* MAP_SHARED + 可写：munmap 时经 pwrite 写回 */
            char *s = (char *)mmap(0, 4096, PROT_READ | PROT_WRITE,
                                   MAP_SHARED, fd2, 0);
            CHECK(s != MAP_FAILED, "MAP_SHARED 可写映射成功");
            if (s != MAP_FAILED) {
                s[0] = 'Z';
                CHECK(munmap(s, 4096) == 0, "MAP_SHARED 映射 munmap 成功");
                char c = 0;
                CHECK(pread(fd2, &c, 1, 0) == 1 && c == 'Z',
                      "MAP_SHARED 修改在 munmap 时写回文件");
            }
            close(fd2);
        }
        unlink(path);
        if (fd >= 0) close(fd);
    }

    TEST_END();
}
