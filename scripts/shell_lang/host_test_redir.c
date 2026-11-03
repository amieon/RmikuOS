/* 宿主侧验证重定向解析。
 *
 * 为什么需要这个测试：`2>&1` 之前在 scan_seps 里就被 `&` 切成两段，
 * 根本到不了 parse_redirect；而 `2>file` / `2>&1 >f` / `>f 2>&1` 的
 * **顺序语义**（POSIX 要求左到右应用，两种写法结果不同）也没法靠读代码确认。
 *
 * 代码全部 include 自 shell.c，不是复制实现。
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <io.h>

#define LINE_SIZE 512

/* 宿主用的是 _open/_dup2 这一套，目标机上是同名的 POSIX 接口，
 * 这里映射一下就能编译同一份 apply_redirects 代码。 */
#define open  _open
#define close _close
#define write _write
#define dup   _dup
#define dup2  _dup2

/* 目标机是 LP64，宿主 LLP64 下 long 只有 4 字节，但这里只用来接 open()
 * 的返回值，32 位足够表示（-1 或小 fd）。 */
typedef long isize;

#include "_redir.inc"

static int fails = 0;
static int total = 0;

static void check(int cond, const char *what) {
    total++;
    if (!cond) {
        fails++;
        printf("FAIL  %s\n", what);
    }
}

static struct segment *parse(const char *s) {
    static char buf[LINE_SIZE];
    static struct segment seg;
    int n = (int)strlen(s);
    if (n >= LINE_SIZE) return 0;
    memcpy(buf, s, n + 1);
    if (parse_redirect(buf, &seg) < 0) return 0;
    return &seg;
}

/* 只看重定向操作，不看剩下的命令行 */
static void expect_ops(const char *script, int nops, const char *what) {
    struct segment *g = parse(script);
    total++;
    if (!g) {
        fails++;
        printf("FAIL  %-28s 解析失败\n", what);
        return;
    }
    if (g->nops != nops) {
        fails++;
        printf("FAIL  %-28s nops=%d 期望 %d\n", what, g->nops, nops);
    }
}

static void expect_op(const char *script, int idx, int fd, int dup_from,
                      const char *name, int append, const char *what) {
    struct segment *g = parse(script);
    total++;
    if (!g || g->nops <= idx) {
        fails++;
        printf("FAIL  %-28s 缺第 %d 个操作\n", what, idx);
        return;
    }
    struct redir_op *op = &g->ops[idx];
    int ok = (op->fd == fd) && (op->dup_from == dup_from) &&
             (op->append == append) && (strcmp(op->name, name) == 0);
    if (!ok) {
        fails++;
        printf("FAIL  %-28s 得到 fd=%d dup=%d name=\"%s\" app=%d, "
               "期望 fd=%d dup=%d name=\"%s\" app=%d\n",
               what, op->fd, op->dup_from, op->name, op->append,
               fd, dup_from, name, append);
    }
}

/* ---- 真实 fd 语义：apply_redirects 到底把数据写到哪 ---- */
static void slurp(const char *path, char *buf, int cap) {
    buf[0] = 0;
    FILE *fp = fopen(path, "rb");
    if (!fp) return;
    int n = (int)fread(buf, 1, (size_t)(cap - 1), fp);
    buf[n] = 0;
    fclose(fp);
}

/* stdout 先指向 orig.txt（模拟"重定向之前的 stdout"），再应用 script 里的
 * 重定向，然后各写一个字节，看它们分别落在哪儿。
 *
 *   >f 2>&1   stdout→f，stderr 复制它 → f 里 AB
 *   2>&1 >f   stderr 复制**旧的** stdout → orig 里 B，f 里只有 A
 * 两种写法只差顺序，结果完全不同 —— 这就是不能用"字段式"表示的原因。 */
static void fd_case(const char *script, const char *exp_f, const char *exp_orig,
                    const char *what) {
    int s1 = dup(1), s2 = dup(2);
    remove("f.txt");
    remove("orig.txt");
    int o = open("orig.txt", O_CREAT | O_TRUNC | O_WRONLY, 0644);
    /* 真终端的 stdout 和 stderr 指向同一个描述，这里必须一起改 ——
     * 只改 1 的话，没被重定向的 stderr 会漏到真实终端上（测试假失败）。 */
    if (o < 0 || dup2(o, 1) < 0 || dup2(o, 2) < 0) {
        if (s1 >= 0) close(s1);
        if (s2 >= 0) close(s2);
        total++;
        fails++;
        printf("FAIL  %-28s 无法准备双写环境\n", what);
        return;
    }
    close(o);

    struct segment *g = parse(script);
    total++;
    if (!g) {
        fails++;
        printf("FAIL  %-28s 解析失败\n", what);
    } else if (apply_redirects(g) < 0) {
        fails++;
        printf("FAIL  %-28s 应用失败\n", what);
    }

    write(1, "A", 1);
    write(2, "B", 1);

    dup2(s1, 1);
    dup2(s2, 2);
    close(s1);
    close(s2);

    char got_f[64], got_o[64];
    slurp("f.txt", got_f, (int)sizeof got_f);
    slurp("orig.txt", got_o, (int)sizeof got_o);

    total++;
    if (strcmp(got_f, exp_f) != 0 || strcmp(got_o, exp_orig) != 0) {
        fails++;
        printf("FAIL  %-28s f=\"%s\"(期望 \"%s\")  orig=\"%s\"(期望 \"%s\")\n",
               what, got_f, exp_f, got_o, exp_orig);
    }
}

int main(void) {
    /* ---- 基本：2>&1 ---- */
    expect_ops("cmd 2>&1", 1, "2>&1 一个操作");
    expect_op ("cmd 2>&1", 0, 2, 1, "", 0, "cmd 2>&1 -> fd2 = fd1");

    /* ---- 输出/输入文件 ---- */
    expect_op("cmd > f",   0, 1, -1, "f", 0, "> f");
    expect_op("cmd >> f",  0, 1, -1, "f", 1, ">> f 追加");
    expect_op("cmd < in",  0, 0, -1, "in", 0, "< in");
    expect_op("cmd 2> err", 0, 2, -1, "err", 0, "2> err");
    expect_op("cmd 2>> err", 0, 2, -1, "err", 1, "2>> err");
    expect_op("cmd 0< in", 0, 0, -1, "in", 0, "0< in 显式 fd");

    /* ---- 顺序必须保留（这是整个 ops 列表存在的理由）---- */
    expect_ops("cmd > f 2>&1", 2, "> f 2>&1 两个操作");
    expect_op ("cmd > f 2>&1", 0, 1, -1, "f", 0, "> f 2>&1 第 0 步");
    expect_op ("cmd > f 2>&1", 1, 2, 1, "", 0,  "> f 2>&1 第 1 步");

    expect_ops("cmd 2>&1 > f", 2, "2>&1 > f 两个操作");
    expect_op ("cmd 2>&1 > f", 0, 2, 1, "", 0,  "2>&1 > f 第 0 步");
    expect_op ("cmd 2>&1 > f", 1, 1, -1, "f", 0, "2>&1 > f 第 1 步");

    /* ---- fd 前缀的边界 ---- */
    /* echo 2>f：无空格 → POSIX 认定 2 是 fd */
    expect_op("echo 2>f", 0, 2, -1, "f", 0, "echo 2>f 无空格 = fd2");
    /* echo 2 >f：有空格 → 2 是普通参数，fd 默认 1 */
    {
        struct segment *g = parse("echo 2 >f");
        total++;
        if (!g || g->nops != 1 || g->ops[0].fd != 1) {
            fails++;
            printf("FAIL  %-28s fd 应为 1（2 是参数）\n", "echo 2 >f 有空格");
        } else {
            /* cmd_str 会保留重定向符之前的原始空白（后续 parse_args 会跳过），
             * 所以比较前自己去掉尾随空白。 */
            char *cs = g->cmd_str;
            int L = (int)strlen(cs);
            while (L > 0 && (cs[L-1] == ' ' || cs[L-1] == '\t')) cs[--L] = 0;
            total++;
            if (strcmp(cs, "echo 2") != 0) {
                fails++;
                printf("FAIL  %-28s cmd_str=\"%s\" 应保留参数 2\n",
                       "echo 2 >f 保留参数", cs);
            }
        }
    }
    /* a1>f：数字紧邻但前面不是空白 → 1 属于命令名 */
    {
        struct segment *g = parse("a1>f");
        total++;
        if (!g || g->nops != 1 || g->ops[0].fd != 1) {
            fails++;
            printf("FAIL  %-28s a1>f 的 1 不是 fd\n", "a1>f 不误判");
        } else {
            total++;
            if (strcmp(g->cmd_str, "a1") != 0) {
                fails++;
                printf("FAIL  %-28s cmd_str=\"%s\" 应为 a1\n", "a1>f 命令名", g->cmd_str);
            }
        }
    }

    /* ---- 多个重定向 ---- */
    expect_ops("cmd <a >b 2>c", 3, "三个重定向");
    expect_op ("cmd <a >b 2>c", 0, 0, -1, "a", 0, "多重重定向第 0 步");
    expect_op ("cmd <a >b 2>c", 1, 1, -1, "b", 0, "多重重定向第 1 步");
    expect_op ("cmd <a >b 2>c", 2, 2, -1, "c", 0, "多重重定向第 2 步");

    /* ---- 引号内的 > 不是重定向 ---- */
    expect_ops("echo \"> f\"", 0, "引号内的 > 不算重定向");
    expect_ops("echo '2>&1'", 0, "引号内的 2>&1 不算重定向");

    /* ---- 越界 ---- */
    {
        char buf[LINE_SIZE];
        strcpy(buf, "cmd");
        for (int i = 0; i < MAX_REDIRS + 1; i++) strcat(buf, " > f");
        total++;
        if (parse(buf) != 0) {
            fails++;
            printf("FAIL  %-28s 超过 MAX_REDIRS 应报错\n", "重定向过多");
        }
    }

    /* ---- 前缀赋值识别 ---- */
    check(is_assign_word("A=1"),    "A=1 是赋值");
    check(is_assign_word("A="),     "A= 空值是赋值");
    check(is_assign_word("_A=1"),   "_A=1 是赋值");
    check(is_assign_word("A1=x"),   "A1=x 是赋值");
    check(!is_assign_word("1A=1"),  "1A=1 数字开头不是赋值");
    check(!is_assign_word("=x"),    "=x 不是赋值");
    check(!is_assign_word("./a=b"), "./a=b 不是赋值");
    check(!is_assign_word("ABC"),   "ABC 无等号不是赋值");
    check(!is_assign_word("A B=1"), "含空格不是赋值");

    {
        char b1[32];
        strcpy(b1, "A=1");
        char *v = 0;
        char *n = split_assign(b1, &v);
        check(strcmp(n, "A") == 0 && strcmp(v, "1") == 0, "split A=1");

        char b2[32];
        strcpy(b2, "A=B=C");
        n = split_assign(b2, &v);
        check(strcmp(n, "A") == 0 && strcmp(v, "B=C") == 0, "split A=B=C 只切第一个");

        char b3[32];
        strcpy(b3, "A=");
        n = split_assign(b3, &v);
        check(strcmp(n, "A") == 0 && v[0] == 0, "split A= 空值");
    }

    /* ---- 真实 fd 指向 ---- */
    fd_case("cmd > f.txt",     "A",  "B", ">f：stderr 留在原处");
    fd_case("cmd 2> f.txt",    "B",  "A", "2>f：stdout 留在原处");
    fd_case("cmd > f.txt 2>&1", "AB", "", ">f 2>&1：两个都进 f");
    fd_case("cmd 2>&1 > f.txt", "A",  "B", "2>&1 >f：stderr 去旧 stdout");
    fd_case("cmd >> f.txt",    "A",  "B", ">>f：追加打开");

    remove("f.txt");
    remove("orig.txt");

    printf("\n%d/%d passed\n", total - fails, total);
    return fails ? 1 : 0;
}
