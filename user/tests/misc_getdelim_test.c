/* getdelim / getline —— 重点是"任意长行"这条，fgets 做不到的那一半。
 *
 * 为什么值得单独测：git 的 strbuf_getwholeline 整个建立在它上面
 * （配置文件、.gitignore、packed-refs 全靠它），而它的契约里有三个
 * 容易写错的点：返回长度是否含分隔符、扩容后 *n 是否如实写回、
 * 最后一行没有换行符时算不算成功。
 */
#include "test.h"

#define FIX "/tmp/gdline.txt"

static int make_file(const char *content, usize len) {
    FILE *f = fopen(FIX, "w");
    if (!f) return -1;
    if (len) fwrite(content, 1, len, f);
    fclose(f);
    return 0;
}

int main() {
    TEST_START("misc_getdelim");

    mkdir("/tmp", 0755);      /* 已存在会失败，无所谓 */

    /* ---- 1. 三行，最后一行没有换行符 ---- */
    const char *txt = "aa\nbbb\ncccc";
    CHECK_EQ(make_file(txt, strlen(txt)), 0, "造测试文件");

    FILE *fp = fopen(FIX, "r");
    if (!fp) {
        FAIL("fopen 读失败");
        TEST_END();
    }

    char *line = (char *)0;
    size_t cap = 0;

    isize n = getline(&line, &cap, fp);
    CHECK_EQ(n, 3, "第一行长度含换行符");
    CHECK_STREQ(line, "aa\n", "第一行内容");
    CHECK(n > 0 && cap > (size_t)n, "库自己分配了缓冲且 *n 是真实容量");

    n = getline(&line, &cap, fp);
    CHECK_EQ(n, 4, "第二行");
    CHECK_STREQ(line, "bbb\n", "第二行内容");

    n = getline(&line, &cap, fp);
    CHECK_EQ(n, 4, "末行无换行也按成功返回");
    CHECK_STREQ(line, "cccc", "末行内容");

    n = getline(&line, &cap, fp);
    CHECK_EQ(n, -1, "真 EOF 返回 -1");

    fclose(fp);
    free(line);

    /* ---- 2. 超长行：一次读 1000 字节，逼出多次扩容 ---- */
    {
        char big[1002];
        for (int i = 0; i < 1000; i++) big[i] = 'x';
        big[1000] = '\n';
        CHECK_EQ(make_file(big, 1001), 0, "造超长行文件");

        fp = fopen(FIX, "r");
        char *l2 = (char *)0;
        size_t c2 = 0;
        isize r = getline(&l2, &c2, fp);
        CHECK_EQ(r, 1001, "超长行整行读入");
        CHECK_GE(c2, 1002, "扩容后容量 ≥ len+1");
        CHECK_EQ(strlen(l2), 1001, "内容长度一致");
        CHECK(l2[0] == 'x' && l2[999] == 'x' && l2[1000] == '\n', "内容完整");
        fclose(fp);
        free(l2);
    }

    /* ---- 3. 自定义分隔符 ---- */
    {
        const char *s = "a;bb;ccc";
        CHECK_EQ(make_file(s, strlen(s)), 0, "造分号文件");

        fp = fopen(FIX, "r");
        char *l3 = (char *)0;
        size_t c3 = 0;
        CHECK_EQ(getdelim(&l3, &c3, ';', fp), 2, "第一段含分隔符");
        CHECK_STREQ(l3, "a;", "第一段内容");
        CHECK_EQ(getdelim(&l3, &c3, ';', fp), 3, "第二段");
        CHECK_STREQ(l3, "bb;", "第二段内容");
        CHECK_EQ(getdelim(&l3, &c3, ';', fp), 3, "末段无分隔符");
        CHECK_STREQ(l3, "ccc", "末段内容");
        CHECK_EQ(getdelim(&l3, &c3, ';', fp), -1, "EOF");
        fclose(fp);
        free(l3);
    }

    /* ---- 4. 空文件：一个字节都没有 → -1，且不能崩 ---- */
    {
        CHECK_EQ(make_file("", 0), 0, "造空文件");
        fp = fopen(FIX, "r");
        char *l4 = (char *)0;
        size_t c4 = 0;
        CHECK_EQ(getline(&l4, &c4, fp), -1, "空文件返回 -1");
        fclose(fp);
        free(l4);
    }

    unlink(FIX);
    TEST_END();
}
