/* grep —— 按模式筛选行。
 *
 * 支持 -i -v -n -c -q -l -h -F -E -e PATTERN --，可读多个文件（'-' 表示 stdin）。
 * 默认 BRE 风格子集：^ $ . * [] \；-E 额外打开 | + ?。**不做分组 ()**，
 * 遇到就当字面量 —— 查日志用不上，省一个回溯节点也省一档栈深。
 *
 * 正则引擎是就地回溯：pattern 逐 atom 吃文本，量词用"先贪到底再退"。
 * 不用 <regex.h>（没有），也不建 AST（省内存、省一整个编译阶段）。
 */
#include "user.h"

#define RE_MAX_DEPTH 3000   /* pattern+文本递归上限，超了当不匹配，防爆栈 */
#define LB_INIT      1024   /* 行缓冲初值，按需翻倍 */


/* 小正则引擎                                                          */


typedef struct {
    const char *p;
    size_t len;
    int ere;      /* 1 = 让 + ? | 当元字符 */
    int icase;
} re_t;

static char re_fold(char c, int icase) {
    if (icase && c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    return c;
}

/* p[0] 起一个 atom 占多少字节。'[' 没闭合就吃到 pattern 尾。 */
static size_t re_atom_len(const char *p, size_t plen) {
    if (!plen) return 0;
    if (p[0] == '\\') return plen > 1 ? 2 : 1;
    if (p[0] == '[') {
        size_t i = 1;
        if (i < plen && p[i] == '^') i++;
        if (i < plen && p[i] == ']') i++;      /* 紧跟在 [ 或 [^ 后的 ] 是字面量 */
        while (i < plen && p[i] != ']') {
            if (p[i] == '\\' && i + 1 < plen) i += 2;
            else i++;
        }
        return (i < plen) ? i + 1 : plen;
    }
    return 1;
}

/* p 指向 '['，plen 是整个 atom 的长度（含两端括号）。 */
static int re_class_match(const char *p, size_t plen, char ch, int icase) {
    size_t i = 1, end = plen;
    int neg = 0, hit = 0;
    char c = re_fold(ch, icase);

    if (end && p[end - 1] == ']') end--;       /* 去掉右括号 */
    if (i < end && p[i] == '^') { neg = 1; i++; }
    if (i < end && p[i] == ']') { if (c == ']') hit = 1; i++; }

    while (i < end) {
        char a = p[i];
        if (a == '\\' && i + 1 < end) { i++; a = p[i]; }
        a = re_fold(a, icase);
        i++;
        if (i + 1 < end && p[i] == '-') {
            char b = p[i + 1];
            i += 2;
            if (b == '\\' && i < end) { b = p[i]; i++; }
            b = re_fold(b, icase);
            if (c >= a && c <= b) hit = 1;
        } else if (c == a) {
            hit = 1;
        }
    }
    return neg ? !hit : hit;
}

static int re_atom_match(const re_t *re, const char *p, size_t alen, char ch) {
    if (!alen) return 0;
    if (p[0] == '.') return 1;
    if (p[0] == '[') return re_class_match(p, alen, ch, re->icase);
    if (p[0] == '\\' && alen > 1)
        return re_fold(ch, re->icase) == re_fold(p[1], re->icase);
    return re_fold(ch, re->icase) == re_fold(p[0], re->icase);
}

static long re_rest(const re_t *re, const char *p, size_t plen,
                    const char *t, size_t tlen, size_t pos, int depth) {
    size_t al, minrep = 0, maxrep = 0, n;
    char quant;
    int is_quant;

    if (depth > RE_MAX_DEPTH) return -1;
    if (!plen) return (long)pos;                   /* pattern 吃完了 */
    if (p[0] == '$' && plen == 1)                  /* 行尾锚 */
        return (pos == tlen) ? (long)pos : -1;

    al = re_atom_len(p, plen);
    quant = (al < plen) ? p[al] : '\0';
    is_quant = (quant == '*') ||
               (re->ere && (quant == '+' || quant == '?'));
    if (!is_quant) {
        if (pos >= tlen) return -1;
        if (!re_atom_match(re, p, al, t[pos])) return -1;
        return re_rest(re, p + al, plen - al, t, tlen, pos + 1, depth + 1);
    }

    if (quant == '+') minrep = 1;
    maxrep = (quant == '?') ? 1 : (size_t)-1;

    n = 0;
    while (n < maxrep && pos + n < tlen &&
           re_atom_match(re, p, al, t[pos + n]))
        n++;

    /* 贪心吃完后逐个回退，第一个能让余下 pattern 匹配上的就是答案 */
    for (;;) {
        long r;
        if (n < minrep) break;
        r = re_rest(re, p + al + 1, plen - al - 1, t, tlen, pos + n, depth + 1);
        if (r >= 0) return r;
        if (n == 0) break;
        n--;
    }
    return -1;
}

/* 单条分支（不含顶层 '|'）在 t 里找一处匹配。 */
static int re_branch(const re_t *re, const char *p, size_t plen,
                     const char *t, size_t tlen) {
    size_t i;
    if (plen && p[0] == '^')                       /* 行首锚 */
        return re_rest(re, p + 1, plen - 1, t, tlen, 0, 0) >= 0;
    for (i = 0; i <= tlen; i++)
        if (re_rest(re, p, plen, t, tlen, i, 0) >= 0) return 1;
    return 0;
}

/* 顶层按 '|' 切分支（跳过 [] 和转义里的）。 */
static int re_search(const re_t *re, const char *t, size_t tlen) {
    const char *p = re->p;
    size_t plen = re->len, bstart = 0, i = 0;

    for (;;) {
        if (i >= plen || (re->ere && p[i] == '|')) {
            if (re_branch(re, p + bstart, i - bstart, t, tlen)) return 1;
            if (i >= plen) break;
            bstart = i + 1;
            i = bstart;
            continue;
        }
        if (p[i] == '\\' && i + 1 < plen) { i += 2; continue; }
        if (p[i] == '[') { i += re_atom_len(p + i, plen - i); continue; }
        i++;
    }
    return 0;
}


/* 行缓冲（没有 realloc，自己 malloc + memcpy + free 翻倍）            */


typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} lb_t;

static int lb_init(lb_t *lb) {
    lb->buf = (char *)malloc(LB_INIT);
    lb->len = 0;
    lb->cap = lb->buf ? LB_INIT : 0;
    return lb->buf ? 0 : -1;
}

static void lb_free(lb_t *lb) {
    if (lb->buf) free(lb->buf);
    lb->buf = NULL;
    lb->len = lb->cap = 0;
}

/* 保留 1 字节给 '\0'，所以装到 cap-1 就翻倍。 */
static int lb_push(lb_t *lb, char c) {
    if (lb->len + 2 > lb->cap) {
        size_t ncap = lb->cap ? lb->cap * 2 : LB_INIT;
        char *nb = (char *)malloc(ncap);
        if (!nb) return -1;
        if (lb->buf) {
            memcpy(nb, lb->buf, lb->len);
            free(lb->buf);
        }
        lb->buf = nb;
        lb->cap = ncap;
    }
    lb->buf[lb->len++] = c;
    lb->buf[lb->len] = '\0';
    return 0;
}


/* 匹配 + 输出                                                         */


typedef struct {
    re_t *res;
    int nres;
    int icase;
    int fixed;
    int inv;
} matcher_t;

static int fixed_match(const char *hay, size_t hlen,
                       const char *pat, size_t plen, int icase) {
    size_t i, j;
    if (!plen) return 1;
    if (plen > hlen) return 0;
    for (i = 0; i + plen <= hlen; i++) {
        for (j = 0; j < plen; j++) {
            if (re_fold(hay[i + j], icase) != re_fold(pat[j], icase)) break;
        }
        if (j == plen) return 1;
    }
    return 0;
}

static int line_matches(const matcher_t *m, const char *line, size_t len) {
    int hit = 0, k;
    for (k = 0; k < m->nres; k++) {
        if (m->fixed) {
            if (fixed_match(line, len, m->res[k].p, m->res[k].len, m->icase))
                hit = 1;
        } else if (re_search(&m->res[k], line, len)) {
            hit = 1;
        }
        if (hit) break;
    }
    return m->inv ? !hit : hit;
}

typedef struct {
    const matcher_t *m;
    const char *name;      /* NULL = 不显示文件名 */
    long lineno;
    long nmatch;
    int number, count, quiet, only_names;
    int matched;           /* 本文件是否有命中 */
    int stop;              /* -q 命中后叫停 */
} out_t;

static void emit(out_t *o, const char *line, size_t len) {
    if (o->only_names) {                       /* -l：文件名只打一次，然后跳下一个文件 */
        if (!o->matched) printf("%s\n", o->name ? o->name : "(standard input)");
        o->matched = 1;
        o->stop = 1;
        return;
    }
    if (!o->count) {
        if (o->name) printf("%s:", o->name);
        if (o->number) printf("%d:", (int)o->lineno);
        fputs(line, stdout);
        putchar('\n');
    }
    o->matched = 1;
    if (o->quiet) o->stop = 1;
}

/* 读一个 fd，逐行匹配。返回 0 正常，-1 读错。 */
static int grep_stream(int fd, out_t *o) {
    static char rbuf[4096];
    lb_t lb;
    isize n;

    o->matched = 0;
    o->stop = 0;
    o->lineno = 0;
    o->nmatch = 0;
    if (lb_init(&lb) < 0) return -1;

    while (!o->stop) {
        isize k;
        n = read(fd, rbuf, sizeof(rbuf));
        if (n < 0) { lb_free(&lb); return -1; }
        if (n == 0) break;

        for (k = 0; k < n; k++) {
            if (rbuf[k] != '\n') {
                /* 装不下就丢尾部，但整行仍参与匹配（前缀匹配好过整行消失） */
                (void)lb_push(&lb, rbuf[k]);
                continue;
            }
            o->lineno++;
            if (line_matches(o->m, lb.buf, lb.len)) {
                o->nmatch++;
                emit(o, lb.buf, lb.len);
            }
            lb.len = 0;
            if (lb.buf) lb.buf[0] = '\0';
            if (o->stop) break;
        }
    }

    /* 末尾没换行的那一行 */
    if (!o->stop && lb.len > 0) {
        o->lineno++;
        if (line_matches(o->m, lb.buf, lb.len)) {
            o->nmatch++;
            emit(o, lb.buf, lb.len);
        }
    }

    lb_free(&lb);
    return 0;
}


/* main                                                                */


static void usage(void) {
    fputs("usage: grep [-ivnclqhFE] [-e PATTERN]... [PATTERN] [FILE]...\n"
          "  -i ignore case   -v invert match   -n line numbers\n"
          "  -c count only    -q quiet          -l files with matches\n"
          "  -h no filename   -F fixed string   -E extended (| + ?)\n", stderr);
}

#define MAX_PATS 32

int main(int argc, char *argv[]) {
    const char *pats[MAX_PATS];
    int npats = 0;
    int icase = 0, inv = 0, number = 0, count = 0, quiet = 0;
    int fixed = 0, ere = 0, only_names = 0, no_fname = 0;
    re_t res[MAX_PATS];
    matcher_t m;
    int i = 1, ret = 0, any = 0;
    int nfiles, multi;

    while (i < argc) {
        const char *a = argv[i];
        int k;

        if (a[0] != '-' || a[1] == '\0') break;     /* 单个 "-" 是文件名 */
        if (strcmp(a, "--") == 0) { i++; break; }
        if (strcmp(a, "--help") == 0) { usage(); return 0; }

        if (a[1] == 'e' && a[2] == '\0') {          /* -e PATTERN（分开写） */
            if (i + 1 >= argc) {
                fputs("grep: option requires an argument -- e\n", stderr);
                return 2;
            }
            if (npats < MAX_PATS) pats[npats++] = argv[++i];
            i++;
            continue;
        }
        if (a[1] == 'e') {                          /* -ePATTERN（贴合写） */
            if (npats < MAX_PATS) pats[npats++] = a + 2;
            i++;
            continue;
        }

        for (k = 1; a[k]; k++) {
            switch (a[k]) {
            case 'i': icase = 1; break;
            case 'v': inv = 1; break;
            case 'n': number = 1; break;
            case 'c': count = 1; break;
            case 'q': quiet = 1; break;
            case 'F': fixed = 1; break;
            case 'E': ere = 1; break;
            case 'l': only_names = 1; break;
            case 'h': no_fname = 1; break;
            default:
                fputs("grep: invalid option -- ", stderr);
                fputc(a[k], stderr);
                fputc('\n', stderr);
                usage();
                return 2;
            }
        }
        i++;
    }

    if (npats == 0) {
        if (i >= argc) { usage(); return 2; }
        pats[npats++] = argv[i++];
    }

    for (int k = 0; k < npats; k++) {
        res[k].p = pats[k];
        res[k].len = strlen(pats[k]);
        res[k].ere = ere && !fixed;
        res[k].icase = icase;
    }
    m.res = res;
    m.nres = npats;
    m.icase = icase;
    m.fixed = fixed;
    m.inv = inv;

    nfiles = argc - i;
    multi = (nfiles > 1) && !no_fname;

    if (nfiles == 0) {
        out_t o;
        memset(&o, 0, sizeof(o));
        o.m = &m;
        o.name = NULL;
        o.number = number; o.count = count;
        o.quiet = quiet; o.only_names = only_names;
        if (grep_stream(0, &o) < 0) {
            fputs("grep: read failed\n", stderr);
            return 2;
        }
        if (count) printf("%d\n", (int)o.nmatch);
        if (o.matched) any = 1;
        if (ret) return ret;
        return any ? 0 : 1;
    }

    for (; i < argc; i++) {
        const char *path = argv[i];
        out_t o;
        int fd, rc;

        if (strcmp(path, "-") == 0) {
            fd = 0;
        } else {
            fd = open(path, O_RDONLY);
            if (fd < 0) {
                fputs("grep: cannot open ", stderr);
                fputs(path, stderr);
                fputs("\n", stderr);
                ret = 2;
                continue;
            }
        }

        memset(&o, 0, sizeof(o));
        o.m = &m;
        o.name = (multi || only_names) ? path : NULL;
        o.number = number; o.count = count;
        o.quiet = quiet; o.only_names = only_names;

        rc = grep_stream(fd, &o);
        if (fd != 0) close(fd);

        if (rc < 0) {
            fputs("grep: read failed: ", stderr);
            fputs(path, stderr);
            fputs("\n", stderr);
            ret = 2;
            continue;
        }
        if (count && !only_names) {
            if (multi) printf("%s:%d\n", path, (int)o.nmatch);
            else       printf("%d\n", (int)o.nmatch);
        }
        if (o.matched) any = 1;
    }

    /* 有匹配 → 0；没匹配 → 1；打不开文件等运行时错误 → 2（POSIX） */
    if (ret) return ret;
    return any ? 0 : 1;
}
