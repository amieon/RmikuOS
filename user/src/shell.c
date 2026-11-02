#include "user.h"


#define LINE_SIZE 512
#define MAX_ARGC  32

#if MAX_ARGC > EXEC_MAX_ARGS
#error "MAX_ARGC must not exceed EXEC_MAX_ARGS (exec ABI limit)"
#endif
#define HISTORY_MAX 16
#define MAX_DIRS 8
#define DIR_LEN 64
#define MAX_MATCHES 32
#define MAX_JOBS 8
#define MAX_SEGMENTS 8
#define MAX_BRACE 32

/* ---- 全局变量 ---- */
static char history[HISTORY_MAX][LINE_SIZE];
static int history_count = 0;
static int history_idx = 0;

static char search_dirs[MAX_DIRS][DIR_LEN];
static int num_dirs = 0;

static char glob_pool[MAX_ARGC][LINE_SIZE];
static int glob_pool_idx = 0;

static char brace_pool[MAX_BRACE][LINE_SIZE];
static int brace_pool_idx = 0;

static char var_pool[MAX_ARGC][LINE_SIZE];
static int var_pool_idx = 0;

/* 最近一条前台命令的退出码，供 $? 展开使用（与 bash 语义一致）。 */
static int g_last_exit = 0;

#define CTL_BREAK     300
#define CTL_CONTINUE  301
#define CTL_MAX       299

/* 循环嵌套层数：break/continue 在循环外要报错，而不是静默返回 300 */
static int g_loop_depth = 0;

enum {
    KW_IF = 0, KW_FOR, KW_WHILE, KW_UNTIL, KW_CASE,
    KW_FI, KW_DONE, KW_ESAC,
    KW_THEN, KW_ELSE, KW_ELIF, KW_DO, KW_IN,
    KW_N
};

/* d > 0：开启一层；d < 0：闭合一层；d == 0：不改变深度 */
static const struct { const char *w; int d; } kwtab[KW_N] = {
    {"if", 1}, {"for", 1}, {"while", 1}, {"until", 1}, {"case", 1},
    {"fi", -1}, {"done", -1}, {"esac", -1},
    {"then", 0}, {"else", 0}, {"elif", 0}, {"do", 0}, {"in", 0},
};

static int is_word_sep(char c) {
    return c == 0 || c == ' ' || c == '\t' || c == '\n' ||
           c == ';' || c == '&' || c == '|' || c == '(' || c == ')' ||
           c == '{' || c == '}' || c == '<' || c == '>';
}

/* p 处是否正好是关键字 kw（只验右边界，左边界由调用方的"命令位置"保证） */
static char *kw_at(const char *p, const char *kw) {
    int n = 0;
    while (kw[n]) n++;
    for (int i = 0; i < n; i++)
        if (p[i] != kw[i]) return 0;
    if (!is_word_sep(p[n])) return 0;
    return (char *)p + n;
}

/* p 处是哪个关键字，返回下标或 -1；*len 回填词长 */
static int kw_lookup(const char *p, int *len) {
    for (int k = 0; k < KW_N; k++) {
        char *e = kw_at(p, kwtab[k].w);
        if (e) {
            if (len) *len = (int)(e - p);
            return k;
        }
    }
    return -1;
}

/* 扫描整串，返回"还剩几层没闭合"。> 0 说明这行还没写完。
 *
 * 这一个函数同时解决两件事：交互式输入的多行续读（提示符 > ），以及
 * source 脚本里跨行的 if/for。不需要完整语法分析 —— 只要关键字配对。 */
static int compound_depth(const char *s) {
    int depth = 0, at_cmd = 1;
    char quote = 0;
    int i = 0;
    while (s[i]) {
        char c = s[i];
        if (quote) {
            if (c == '\\' && quote == '"' && s[i+1]) { i += 2; continue; }
            if (c == quote) quote = 0;
            i++; continue;
        }
        if (c == '\\' && s[i+1]) { i += 2; continue; }
        if (c == '"' || c == '\'') { quote = c; at_cmd = 0; i++; continue; }
        if (c == '#' && (i == 0 || s[i-1] == ' ' || s[i-1] == '\t')) {
            while (s[i] && s[i] != '\n') i++;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n') { i++; continue; }
        if (c == ';' || c == '|' || c == '&') { at_cmd = 1; i++; continue; }

        /* 只在**命令位置**认关键字：`echo if` 里的 if 只是参数，
         * 若把它算成开括号，输入就会一直等 `>` 等不到头。 */
        if (at_cmd) {
            int wl = 0;
            int k = kw_lookup(s + i, &wl);
            if (k >= 0) {
                if (kwtab[k].d < 0) { if (depth > 0) depth--; }
                else depth += kwtab[k].d;
                if (depth < 0) depth = 0;
                at_cmd = (k == KW_FOR) ? 0 : 1;   /* for 后面是变量名 */
                i += wl;
                continue;
            }
        }
        at_cmd = 0;
        i++;
    }
    return depth;
}

/* 裁掉"词首 #"到行尾的注释（引号内的不算）。
 *
 * 复合命令必须先裁剪再分析：`for i in 1 2; do # 数一下` 里的 # 如果留到
 * find_kw0，会让它一路扫到串尾都找不到 done，报 "missing done" ——
 * 一条完全正确的循环被行尾注释带崩。 */
static void strip_comment(char *s) {
    char quote = 0;
    for (int i = 0; s[i]; i++) {
        char c = s[i];
        if (quote) {
            if (c == '\\' && quote == '"' && s[i+1]) { i++; continue; }
            if (c == quote) quote = 0;
            continue;
        }
        if (c == '\\' && s[i+1]) { i++; continue; }
        if (c == '"' || c == '\'') { quote = c; continue; }
        if (c == '#' && (i == 0 || s[i-1] == ' ' || s[i-1] == '\t')) {
            s[i] = 0;
            return;
        }
    }
}

/* 语句分隔符 */
#define SEP_SEMI 1
#define SEP_AND  2      /* && */
#define SEP_OR   3      /* || */
#define SEP_BG   4      /* 单个 & */

struct sep { int pos; int len; int kind; };

/* 收集相对深度 0 的语句分隔符。复合命令体内部的分号不在其列 ——
 * 这正是 `if a; then b; fi && c` 能正确分层的前提。 */
static int scan_seps(const char *s, struct sep *out, int max) {
    int n = 0, depth = 0, at_cmd = 1;
    char quote = 0;
    int i = 0;
    while (s[i]) {
        char c = s[i];
        if (quote) {
            if (c == '\\' && quote == '"' && s[i+1]) { i += 2; continue; }
            if (c == quote) quote = 0;
            i++; continue;
        }
        if (c == '\\' && s[i+1]) { i += 2; continue; }
        if (c == '"' || c == '\'') { quote = c; at_cmd = 0; i++; continue; }
        if (c == ' ' || c == '\t' || c == '\n') { i++; continue; }

        if (at_cmd) {
            int wl = 0;
            int k = kw_lookup(s + i, &wl);
            if (k >= 0) {
                /* 关键字只在这时才算关键字。这一条让 `for i in if` 里的
                 * if 不会被当成开括号 —— 变量名里出现关键字是合法的。 */
                depth += kwtab[k].d;
                if (depth < 0) depth = 0;
                /* for 后面是变量名，不是命令位置 */
                at_cmd = (k == KW_FOR) ? 0 : 1;
                i += wl;
                continue;
            }
        }

        if (c == ';') {
            if (depth == 0 && n < max) { out[n].pos = i; out[n].len = 1; out[n].kind = SEP_SEMI; n++; }
            at_cmd = 1; i++; continue;
        }
        if (c == '&') {
            /* `2>&1` / `>&2` 里的 & 属于 fd 复制，不是后台分隔符。
             * 之前这里无条件当 SEP_BG，`cmd 2>&1` 就被切成 "cmd 2>" 和 "1"
             * 两段，前一段的 `>` 没有目标 → "syntax error in redirection"。
             * POSIX 要求 `>&` 紧邻，所以只看紧前一个字符就够。 */
            if (i > 0 && (s[i-1] == '>' || s[i-1] == '<')) { i++; continue; }
            if (s[i+1] == '&') {
                if (depth == 0 && n < max) { out[n].pos = i; out[n].len = 2; out[n].kind = SEP_AND; n++; }
                i += 2;
            } else {
                if (depth == 0 && n < max) { out[n].pos = i; out[n].len = 1; out[n].kind = SEP_BG; n++; }
                i++;
            }
            at_cmd = 1; continue;
        }
        if (c == '|') {
            if (s[i+1] == '|') {
                if (depth == 0 && n < max) { out[n].pos = i; out[n].len = 2; out[n].kind = SEP_OR; n++; }
                i += 2;
            } else {
                i++;        /* 单管道交给 run_pipeline */
            }
            at_cmd = 1; continue;
        }
        at_cmd = 0;
        i++;
    }
    return n;
}

/* 在 s 里找相对深度 0 的指定关键字，返回位置，*id 回填下标。
 *
 * 闭合关键字（fi/done/esac）要先看深度：深度为 0 时它闭合的才是
 * "本层"。否则 `if a; then if b; then c; fi; fi` 里第一个 fi 会被外层
 * 认领，内层 if 就永远少一个结尾。 */
static char *find_kw0(char *s, const int *ids, int nids, int *id) {
    int depth = 0, at_cmd = 1;
    char quote = 0;
    int i = 0;
    while (s[i]) {
        char c = s[i];
        if (quote) {
            if (c == '\\' && quote == '"' && s[i+1]) { i += 2; continue; }
            if (c == quote) quote = 0;
            i++; continue;
        }
        if (c == '\\' && s[i+1]) { i += 2; continue; }
        if (c == '"' || c == '\'') { quote = c; at_cmd = 0; i++; continue; }
        if (c == ' ' || c == '\t' || c == '\n') { i++; continue; }
        if (c == '#' && (i == 0 || is_word_sep(s[i-1]))) { break; }
        if (c == ';' || c == '|' || c == '&') { at_cmd = 1; i++; continue; }

        if (at_cmd) {
            int wl = 0;
            int k = kw_lookup(s + i, &wl);
            if (k >= 0) {
                if (kwtab[k].d < 0) {
                    if (depth == 0) {
                        for (int t = 0; t < nids; t++)
                            if (ids[t] == k) { *id = k; return s + i; }
                        return 0;       /* 括号不配对 */
                    }
                    depth--;
                } else if (kwtab[k].d > 0) {
                    depth++;
                } else if (depth == 0) {
                    for (int t = 0; t < nids; t++)
                        if (ids[t] == k) { *id = k; return s + i; }
                }
                at_cmd = (k == KW_FOR) ? 0 : 1;
                i += wl;
                continue;
            }
        }
        at_cmd = 0;
        i++;
    }
    return 0;
}


struct job {
    int used;
    int id;
    /* 存的是进程组号，不是 pid：一条管线是多个进程，`fg`/`bg`/Ctrl+C
     * 都要对整组生效。单命令时 pgid == pid（fork 后立刻 setpgid(child, child)）。 */
    isize pgid;
    int stopped;   /* 1 = 被 Ctrl+Z 停住，等 fg/bg 继续 */
    int why;       /* 停住它的信号号（WSTOPSIG），给 fg 的 $? 用 */
    char cmd[LINE_SIZE];
};
static struct job jobs[MAX_JOBS];
static int next_job_id = 1;

/* ---- 工具函数 ---- */
static int streq(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == 0 && b[i] == 0;
}

static int strlen_(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static int isdigit_(char c) { return c >= '0' && c <= '9'; }

static int parse_int_prefix__(const char *s, int *out) {
    int n = 0;
    int val = 0;
    while (isdigit_(s[n])) {
        val = val * 10 + (s[n] - '0');
        n++;
    }
    if (n == 0) return 0;
    *out = val;
    return n;
}

static int has_slash(const char *s) {
    for (int i = 0; s[i]; i++) {
        if (s[i] == '/') return 1;
    }
    return 0;
}

static void redraw_line(const char *prompt, const char *buf, int cursor) {
    int len = strlen_(buf);
    putchar('\r');
    fputs("\x1b[2K", stdout);
    fputs(prompt, stdout);
    fputs(buf, stdout);
    fflush(stdout);                 /* prompt 没换行，必须刷 */
    for (int i = 0; i < len - cursor; i++) {
        putchar('\b');
    }
    fflush(stdout);                 /* 光标回退也要刷 */
}

/* ---- Tab 补全 ---- */
static int get_token_start(const char *buf, int cursor) {
    int start = cursor;
    while (start > 0 && buf[start - 1] != ' ' && buf[start - 1] != '\t')
        start--;
    return start;
}

static int is_first_token(const char *buf, int cursor) {
    for (int i = 0; i < cursor; i++)
        if (buf[i] == ' ' || buf[i] == '\t')
            return 0;
    return 1;
}

static int strncmp_(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (a[i] == 0) return 0;
    }
    return 0;
}

static int lcp_len(char matches[][LINE_SIZE], int count) {
    if (count == 0) return 0;
    int len = 0;
    while (matches[0][len]) {
        for (int i = 1; i < count; i++)
            if (matches[i][len] != matches[0][len])
                return len;
        len++;
    }
    return len;
}

static void replace_token(char *buf, int *len, int *cursor, int token_start,
                          int token_len, const char *replacement) {
    int repl_len = strlen_(replacement);
    int tail = *len - (token_start + token_len);
    for (int i = tail; i >= 0; i--)
        buf[token_start + repl_len + i] = buf[token_start + token_len + i];
    for (int i = 0; i < repl_len; i++)
        buf[token_start + i] = replacement[i];
    *len = token_start + repl_len + tail;
    *cursor = token_start + repl_len;
    buf[*len] = 0;
}

static int scan_dir(const char *dir, const char *prefix,
                    char matches[][LINE_SIZE], int max_matches) {
    int fd = open(dir, O_RDONLY);
    if (fd < 0) return 0;

    int count = 0;
    int prefix_len = strlen_(prefix);

    while (count < max_matches) {
        struct dirent entries[16];
        isize n = getdents(fd, entries, sizeof(entries));
        if (n <= 0) break;

        int num = n / sizeof(struct dirent);
        for (int i = 0; i < num && count < max_matches; i++) {
            int nl = entries[i].name_len;
            if (nl > 56) nl = 56;

            if (nl >= 1 && entries[i].name[0] == '.') {
                if (nl == 1) continue;
                if (nl == 2 && entries[i].name[1] == '.') continue;
            }

            char name[64];
            if (nl > 63) nl = 63;
            for (int j = 0; j < nl; j++) name[j] = entries[i].name[j];
            name[nl] = 0;

            if (prefix_len == 0 || strncmp_(name, prefix, prefix_len) == 0) {
                copy_str(matches[count], name, LINE_SIZE);
                count++;
            }
        }
    }

    close(fd);
    return count;
}

static int complete_path(const char *prefix, char matches[][LINE_SIZE], int max_matches) {
    char dir[LINE_SIZE];
    char file_prefix[LINE_SIZE];

    int last_slash = -1;
    for (int i = 0; prefix[i]; i++)
        if (prefix[i] == '/') last_slash = i;

    if (last_slash >= 0) {
        int dir_len = last_slash + 1;
        if (dir_len > LINE_SIZE - 1) dir_len = LINE_SIZE - 1;
        for (int i = 0; i < dir_len; i++) dir[i] = prefix[i];
        dir[dir_len] = 0;

        int j = 0;
        for (int i = last_slash + 1; prefix[i]; i++)
            file_prefix[j++] = prefix[i];
        file_prefix[j] = 0;
    } else {
        if (getcwd(dir, sizeof(dir)) == NULL) return 0;
        int dlen = strlen_(dir);
        if (dlen > 0 && dir[dlen - 1] != '/') {
            dir[dlen] = '/';
            dir[dlen + 1] = 0;
        }
        copy_str(file_prefix, prefix, LINE_SIZE);
    }

    int count = scan_dir(dir, file_prefix, matches, max_matches);

    /* 如果 prefix 带了目录前缀，匹配结果也要保留该前缀 */
    if (last_slash >= 0) {
        for (int i = 0; i < count; i++) {
            char tmp[LINE_SIZE];
            copy_str(tmp, matches[i], LINE_SIZE);
            int dlen = strlen_(dir);
            int nlen = strlen_(tmp);
            if (dlen + nlen >= LINE_SIZE) continue;
            for (int j = 0; j < dlen; j++) matches[i][j] = dir[j];
            for (int j = 0; j < nlen; j++) matches[i][dlen + j] = tmp[j];
            matches[i][dlen + nlen] = 0;
        }
    }

    return count;
}

static int complete_command(const char *prefix, char matches[][LINE_SIZE], int max_matches) {
    int count = 0;
    int prefix_len = strlen_(prefix);

    const char *builtins[] = {
        "help", "exit", "pwd", "cd", "mkdir", "touch", "rm", "rmdir", "rename", "mv",
        "whoami", "chmod", "chown",
        "shutdown", "ls", "cat", "clear", "jobs", "fg", "bg",
        "export", "env", "unset", "source", NULL
    };

    for (int i = 0; builtins[i] && count < max_matches; i++) {
        if (strncmp_(builtins[i], prefix, prefix_len) == 0)
            copy_str(matches[count++], builtins[i], LINE_SIZE);
    }

    for (int d = 0; d < num_dirs && count < max_matches; d++) {
        char tmp[MAX_MATCHES][LINE_SIZE];
        int n = scan_dir(search_dirs[d], prefix, tmp, max_matches - count);
        for (int i = 0; i < n && count < max_matches; i++) {
            int dup = 0;
            for (int j = 0; j < count; j++)
                if (streq(matches[j], tmp[i])) { dup = 1; break; }
            if (!dup) copy_str(matches[count++], tmp[i], LINE_SIZE);
        }
    }
    return count;
}

/* ---- 大括号展开 Brace Expansion ---- */
static int brace_expand_token(const char *token, char *outv[], int max_out) {
    int brace_open = -1;
    int brace_close = -1;
    int depth = 0;
    for (int i = 0; token[i]; i++) {
        if (token[i] == '{') {
            if (depth == 0) brace_open = i;
            depth++;
        } else if (token[i] == '}') {
            depth--;
            if (depth == 0) {
                brace_close = i;
                break;
            }
        }
    }
    if (brace_open < 0 || brace_close < 0 || brace_open > brace_close)
        return 0;

    char prefix[LINE_SIZE];
    char suffix[LINE_SIZE];
    for (int i = 0; i < brace_open && i < LINE_SIZE - 1; i++) prefix[i] = token[i];
    prefix[brace_open > LINE_SIZE - 1 ? LINE_SIZE - 1 : brace_open] = 0;

    int suffix_len = strlen_(token) - brace_close - 1;
    if (suffix_len > LINE_SIZE - 1) suffix_len = LINE_SIZE - 1;
    for (int i = 0; i < suffix_len; i++) suffix[i] = token[brace_close + 1 + i];
    suffix[suffix_len] = 0;

    char body[LINE_SIZE];
    int body_len = brace_close - brace_open - 1;
    if (body_len > LINE_SIZE - 1) body_len = LINE_SIZE - 1;
    for (int i = 0; i < body_len; i++) body[i] = token[brace_open + 1 + i];
    body[body_len] = 0;

    /* 检查数字范围 {1..3} */
    int start_num, end_num;
    int consumed = parse_int_prefix__(body, &start_num);
    if (consumed > 0 && body[consumed] == '.' && body[consumed + 1] == '.') {
        int consumed2 = parse_int_prefix__(body + consumed + 2, &end_num);
        if (consumed2 > 0 && body[consumed + 2 + consumed2] == 0) {
            int count = 0;
            if (start_num <= end_num) {
                for (int n = start_num; n <= end_num && count < max_out && brace_pool_idx < MAX_BRACE; n++) {
                    char num_str[16];
                    int num_len = 0;
                    int tmp = n;
                    if (tmp == 0) num_str[num_len++] = '0';
                    while (tmp > 0) {
                        num_str[num_len++] = '0' + (tmp % 10);
                        tmp /= 10;
                    }
                    for (int l = 0; l < num_len / 2; l++) {
                        char t = num_str[l];
                        num_str[l] = num_str[num_len - 1 - l];
                        num_str[num_len - 1 - l] = t;
                    }
                    num_str[num_len] = 0;

                    int pl = strlen_(prefix);
                    int sl = strlen_(suffix);
                    if (pl + num_len + sl >= LINE_SIZE) continue;
                    for (int j = 0; j < pl; j++) brace_pool[brace_pool_idx][j] = prefix[j];
                    for (int j = 0; j < num_len; j++) brace_pool[brace_pool_idx][pl + j] = num_str[j];
                    for (int j = 0; j < sl; j++) brace_pool[brace_pool_idx][pl + num_len + j] = suffix[j];
                    brace_pool[brace_pool_idx][pl + num_len + sl] = 0;
                    outv[count++] = brace_pool[brace_pool_idx++];
                }
            }
            return count;
        }
    }

    /* 逗号分隔 {a,b,c} */
    int count = 0;
    int start = 0;
    for (int i = 0; i <= body_len && count < max_out && brace_pool_idx < MAX_BRACE; i++) {
        if (body[i] == ',' || body[i] == 0) {
            int item_len = i - start;
            if (item_len > LINE_SIZE - 1) item_len = LINE_SIZE - 1;

            int pl = strlen_(prefix);
            int sl = strlen_(suffix);
            if (pl + item_len + sl >= LINE_SIZE) { start = i + 1; continue; }

            for (int j = 0; j < pl; j++) brace_pool[brace_pool_idx][j] = prefix[j];
            for (int j = 0; j < item_len; j++) brace_pool[brace_pool_idx][pl + j] = body[start + j];
            for (int j = 0; j < sl; j++) brace_pool[brace_pool_idx][pl + item_len + j] = suffix[j];
            brace_pool[brace_pool_idx][pl + item_len + sl] = 0;

            outv[count++] = brace_pool[brace_pool_idx++];
            start = i + 1;
        }
    }

    if (count == 0) return 0;
    return count;
}

/* ---- 通配符 + 字符类 ---- */
static int has_glob(const char *s) {
    for (int i = 0; s[i]; i++)
        if (s[i] == '*' || s[i] == '?' || s[i] == '[') return 1;
    return 0;
}

static int match_pattern(const char *pat, const char *str) {
    const char *last_star = NULL;
    const char *last_star_str = NULL;

    while (*str) {
        if (*pat == *str || *pat == '?') {
            pat++;
            str++;
        } else if (*pat == '*') {
            while (pat[1] == '*') pat++;
            last_star = pat++;
            last_star_str = str;
        } else if (*pat == '[') {
            pat++;
            int negate = 0;
            if (*pat == '!' || *pat == '^') {
                negate = 1;
                pat++;
            }
            int match = 0;
            while (*pat && *pat != ']') {
                if (pat[1] == '-' && pat[2] != ']' && pat[2] != '\0') {
                    if (*str >= pat[0] && *str <= pat[2]) match = 1;
                    pat += 3;
                } else {
                    if (*pat == *str) match = 1;
                    pat++;
                }
            }
            if (*pat == ']') pat++;
            if (match == negate) return 0;
            str++;
        } else if (last_star) {
            pat = last_star + 1;
            str = ++last_star_str;
        } else {
            return 0;
        }
    }

    while (*pat == '*') pat++;
    return *pat == '\0';
}

static int expand_token(const char *token, char *outv[], int max_out, int is_quoted) {
    if (is_quoted || !has_glob(token)) {
        if (glob_pool_idx >= MAX_ARGC) return 0;
        copy_str(glob_pool[glob_pool_idx], token, LINE_SIZE);
        outv[0] = glob_pool[glob_pool_idx++];
        return 1;
    }

    char dir[LINE_SIZE];
    char pat[LINE_SIZE];
    int last_slash = -1;
    for (int i = 0; token[i]; i++)
        if (token[i] == '/') last_slash = i;

    if (last_slash >= 0) {
        int dl = last_slash + 1;
        if (dl >= LINE_SIZE) dl = LINE_SIZE - 1;
        for (int i = 0; i < dl; i++) dir[i] = token[i];
        dir[dl] = 0;
        int j = 0;
        for (int i = last_slash + 1; token[i]; i++) pat[j++] = token[i];
        pat[j] = 0;
    } else {
        if (getcwd(dir, sizeof(dir)) == NULL) return 0;
        int dlen = strlen_(dir);
        if (dlen > 0 && dir[dlen - 1] != '/') {
            dir[dlen] = '/';
            dir[dlen + 1] = 0;
        }
        copy_str(pat, token, LINE_SIZE);
    }

    int count = 0;
    int fd = open(dir, O_RDONLY);
    if (fd < 0) {
        if (glob_pool_idx >= MAX_ARGC) return 0;
        copy_str(glob_pool[glob_pool_idx], token, LINE_SIZE);
        outv[0] = glob_pool[glob_pool_idx++];
        return 1;
    }

    while (count < max_out && glob_pool_idx < MAX_ARGC) {
        struct dirent entries[16];
        isize n = getdents(fd, entries, sizeof(entries));
        if (n <= 0) break;

        int num = n / sizeof(struct dirent);
        for (int i = 0; i < num && count < max_out && glob_pool_idx < MAX_ARGC; i++) {
            int nl = entries[i].name_len;
            if (nl > 56) nl = 56;

            if (nl >= 1 && entries[i].name[0] == '.') continue;

            char name[64];
            if (nl > 63) nl = 63;
            for (int j = 0; j < nl; j++) name[j] = entries[i].name[j];
            name[nl] = 0;

            if (match_pattern(pat, name)) {
                int dl = strlen_(dir);
                int nlen = strlen_(name);
                if (dl + nlen >= LINE_SIZE) continue;
                for (int j = 0; j < dl; j++) glob_pool[glob_pool_idx][j] = dir[j];
                for (int j = 0; j < nlen; j++) glob_pool[glob_pool_idx][dl + j] = name[j];
                glob_pool[glob_pool_idx][dl + nlen] = 0;
                outv[count++] = glob_pool[glob_pool_idx++];
            }
        }
    }
    close(fd);

    if (count == 0) {
        if (glob_pool_idx >= MAX_ARGC) return 0;
        copy_str(glob_pool[glob_pool_idx], token, LINE_SIZE);
        outv[0] = glob_pool[glob_pool_idx++];
        return 1;
    }
    return count;
}

static int expand_args(int argc, char *argv[], char *outv[], int quoted[]) {
    glob_pool_idx = 0;
    brace_pool_idx = 0;
    int outc = 0;
    for (int i = 0; i < argc && outc < MAX_ARGC; i++) {
        if (quoted[i]) {
            if (glob_pool_idx >= MAX_ARGC) break;
            copy_str(glob_pool[glob_pool_idx], argv[i], LINE_SIZE);
            outv[outc++] = glob_pool[glob_pool_idx++];
            continue;
        }

        char *brace_results[MAX_ARGC];
        int brace_count = brace_expand_token(argv[i], brace_results, MAX_ARGC - outc);
        if (brace_count <= 0) {
            brace_results[0] = argv[i];
            brace_count = 1;
        }

        for (int b = 0; b < brace_count && outc < MAX_ARGC; b++) {
            int n = expand_token(brace_results[b], &outv[outc], MAX_ARGC - outc, 0);
            if (n == 0) break;
            outc += n;
        }
    }
    return outc;
}

/* ---- 变量展开 $VAR / ${VAR} ---- */
static int is_env_name_char(char c) {
    if (c >= 'a' && c <= 'z') return 1;
    if (c >= 'A' && c <= 'Z') return 1;
    if (c >= '0' && c <= '9') return 1;
    if (c == '_') return 1;
    return 0;
}

/* 把整数转成十进制字符串写入 out，返回写入长度（不含结尾）。val 可为负。 */
static int itoa_(int val, char *out) {
    int tmp[12];
    int n = 0;
    int v = val;
    if (v < 0) { out[n++] = '-'; v = -v; }
    if (v == 0) { out[n++] = '0'; return n; }
    int digits = 0;
    while (v > 0) { tmp[digits++] = '0' + (v % 10); v /= 10; }
    for (int i = digits - 1; i >= 0; i--) out[n++] = tmp[i];
    return n;
}

/* 展开 $VAR / ${VAR} / $? / ${?}。
 * 引号语义（bash 风格）：
 *   - quoted[]==1（单引号）：完全保护，$ 原样输出，不展开；
 *   - quoted[]==2（双引号）：仍展开 $（含 $?）；
 *   - 0（无引号）：展开 $。
 * 双引号展开结果标记为整体（out_quoted=1），不再做 glob/分词，符合 bash。
 * 结果写入 var_pool（独立于原始 line，避免展开后变长覆盖相邻 token）。 */
static int expand_variables(int argc, char *argv[], int quoted[],
                             char *outv[], int out_quoted[]) {
    var_pool_idx = 0;
    int outc = 0;
    for (int i = 0; i < argc && outc < MAX_ARGC; i++) {
        if (quoted[i] == 1) {
            /* 单引号：完全保护，原样输出 */
            if (var_pool_idx >= MAX_ARGC) break;
            copy_str(var_pool[var_pool_idx], argv[i], LINE_SIZE);
            outv[outc] = var_pool[var_pool_idx++];
            out_quoted[outc] = 1;
            outc++;
            continue;
        }
        char *src = argv[i];
        char *dst = var_pool[var_pool_idx];
        int o = 0;
        int j = 0;
        while (src[j] && o < LINE_SIZE - 1) {
            if (src[j] == '$' && src[j + 1]) {
                if (src[j + 1] == '?') {
                    /* $? : 上条命令退出码 */
                    if (o + 12 < LINE_SIZE - 1) {
                        int len = itoa_(g_last_exit, dst + o);
                        o += len;
                    }
                    j += 2;
                    continue;
                } else if (src[j + 1] == '{') {
                    int k = j + 2;
                    while (src[k] && src[k] != '}') k++;
                    int namelen = k - (j + 2);
                    if (namelen > 0 && src[k] == '}') {
                        char name[64];
                        int nl = namelen < 63 ? namelen : 63;
                        for (int t = 0; t < nl; t++) name[t] = src[j + 2 + t];
                        name[nl] = 0;
                        const char *val = 0;
                        int from_exit = 0;
                        if (name[0] == '?' && name[1] == 0) {
                            from_exit = 1;          /* ${?} */
                        } else {
                            val = getenv(name);
                        }
                        if (from_exit) {
                            if (o + 12 < LINE_SIZE - 1) {
                                int len = itoa_(g_last_exit, dst + o);
                                o += len;
                            }
                        } else if (val) {
                            for (int t = 0; val[t] && o < LINE_SIZE - 1; t++)
                                dst[o++] = val[t];
                        }
                        j = k + 1;
                        continue;
                    }
                    /* 没配对的 {，原样输出 $ */
                } else if (is_env_name_char(src[j + 1])) {
                    int k = j + 1;
                    while (is_env_name_char(src[k])) k++;
                    int namelen = k - (j + 1);
                    char name[64];
                    int nl = namelen < 63 ? namelen : 63;
                    for (int t = 0; t < nl; t++) name[t] = src[j + 1 + t];
                    name[nl] = 0;
                    const char *val = getenv(name);
                    if (val) {
                        for (int t = 0; val[t] && o < LINE_SIZE - 1; t++)
                            dst[o++] = val[t];
                    }
                    j = k;
                    continue;
                }
                /* 孤立的 $ 或 $ 后接非法字符：原样输出 $ */
                dst[o++] = '$';
                j++;
                continue;
            }
            dst[o++] = src[j++];
        }
        dst[o] = 0;
        if (var_pool_idx >= MAX_ARGC) break;
        outv[outc] = var_pool[var_pool_idx++];
        out_quoted[outc] = (quoted[i] == 2) ? 1 : 0;
        outc++;
    }
    return outc;
}

/* ---- 后台作业 ---- */
static int add_job(isize pgid, const char *cmd, int stopped) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!jobs[i].used) {
            jobs[i].used = 1;
            jobs[i].id = next_job_id++;
            jobs[i].pgid = pgid;
            jobs[i].stopped = stopped;
            jobs[i].why = stopped ? SIGTSTP : 0;
            copy_str(jobs[i].cmd, cmd, LINE_SIZE);
            return jobs[i].id;
        }
    }
    return 0;
}

/* 无参数：最近一个停住的作业（按 id 最大挑）——bash 的 fg/bg 也是这样。
 * 有参数：%N 或 N 当作业号，否则当进程组号。 */
static struct job *find_job(const char *arg) {
    if (!arg) {
        struct job *best = 0;
        for (int i = 0; i < MAX_JOBS; i++) {
            if (!jobs[i].used || !jobs[i].stopped) continue;
            if (!best || jobs[i].id > best->id) best = &jobs[i];
        }
        if (best) return best;
        for (int i = 0; i < MAX_JOBS; i++) {
            if (!jobs[i].used) continue;
            if (!best || jobs[i].id > best->id) best = &jobs[i];
        }
        return best;
    }
    if (arg[0] == '%') arg++;
    int want = atoi(arg);
    for (int i = 0; i < MAX_JOBS; i++)
        if (jobs[i].used && jobs[i].id == want) return &jobs[i];
    for (int i = 0; i < MAX_JOBS; i++)
        if (jobs[i].used && jobs[i].pgid == (isize)want) return &jobs[i];
    return 0;
}

/* 一个作业可能是整条管线，所以按组回收：循环到没有新状态为止。
 * 循环结束的 r：> 0 表示还有成员没报完、0 表示暂时没变化、< 0 表示组里
 * 已经没有子进程了 —— 只有最后一种才算"整个作业结束"。 */
static void reap_jobs(void) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!jobs[i].used) continue;

        int status = 0, stopped = -1, stop_sig = 0, exited = 0;
        isize r;
        for (;;) {
            r = waitpid(-jobs[i].pgid, &status, WNOHANG | WUNTRACED | WCONTINUED);
            if (r <= 0) break;
            if (WIFSTOPPED(status)) {
                stopped = 1;
                stop_sig = WSTOPSIG(status);
            } else if (WIFCONTINUED(status)) {
                stopped = 0;
            } else {
                exited++;
            }
        }

        if (stopped == 1) {
            jobs[i].stopped = 1;
            jobs[i].why = stop_sig;
            printf("\n[%d]+  Stopped  %s\n", jobs[i].id, jobs[i].cmd);
        } else if (stopped == 0) {
            jobs[i].stopped = 0;
            printf("\n[%d]+  Continued  %s\n", jobs[i].id, jobs[i].cmd);
        } else if (exited > 0 && r < 0) {
            printf("[%d] done %s\n", jobs[i].id, jobs[i].cmd);
            jobs[i].used = 0;
        }
    }
}

static void print_jobs(void) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].used) {
            printf("[%d] %s %s\n", jobs[i].id,
                   jobs[i].stopped ? "Stopped" : "Running", jobs[i].cmd);
        }
    }
}

/* ---- read_line ---- */
static int read_line(const char *prompt, char *buf, int max_len) {
    int len = 0;
    int cursor = 0;
    char saved_line[LINE_SIZE];
    for (int i = 0; i < LINE_SIZE; i++) saved_line[i] = 0;
    int saved_len = 0;
    int in_history = 0;

    set_echo(0);

    buf[0] = 0;
    history_idx = history_count;
    fputs(prompt, stdout);
    fflush(stdout);        

    while (len < max_len - 1) {
        char ch = 0;
        isize n = read(0, &ch, 1);

        if (n <= 0) continue;
        if (ch == '\r') ch = '\n';

        if (ch == '\n') {
            /* POSIX 行续：行尾**奇数个**反斜杠（即不是 `\\` 转义出来的字面反斜杠）
             * 表示这一行还没完，把下一行接上来。
             *
             * 没有这个，粘贴带续行的命令会被拆成两条独立命令行：curl 那条就报
             * `option : blank argument where content is expected` —— 而人看到的是
             * "命令跑了一下然后报了个莫名其妙的错"，很难想到是分词层。
             * 奇数才续行，是为了让 `echo a\\` 仍然以一个字面反斜杠结尾。 */
            int bs = 0;
            while (bs < len && buf[len - 1 - bs] == '\\') bs++;
            if (len > 0 && (bs & 1)) {
                len--;
                cursor = len;
                buf[len] = 0;
                fputs("\n> ", stdout);
                fflush(stdout);
                continue;
            }
            /* 复合命令还没闭合（if..fi / for..done）：换行不能算结束。
             * 把换行就地改成 ';' 接下一行，`for i in a b` + 回车 + `do`
             * 就拼成 `for i in a b;do`，与单行写法完全等价。 */
            if (len > 0 && compound_depth(buf) > 0) {
                if (len < max_len - 2) {
                    buf[len++] = ';';
                    buf[len] = 0;
                    cursor = len;
                    fputs("\n> ", stdout);
                    fflush(stdout);
                    continue;
                }
                /* 这一行已经塞满：不再接，交给 exec 报语法错，别静默吞掉 */
                buf[len] = 0;
            }
            fputs("\n", stdout);
            break;
        }

        if (ch == '\t') {
            int token_start = get_token_start(buf, cursor);
            int token_len = cursor - token_start;
            char prefix[LINE_SIZE];
            for (int i = 0; i < token_len; i++) prefix[i] = buf[token_start + i];
            prefix[token_len] = 0;

            /* 含 '/' 的 token 一律按路径补全，不按命令补全 */
            int is_cmd = is_first_token(buf, cursor) && !has_slash(prefix);
            char matches[MAX_MATCHES][LINE_SIZE];
            int n = is_cmd ? complete_command(prefix, matches, MAX_MATCHES)
                           : complete_path(prefix, matches, MAX_MATCHES);

            if (n == 0) {
                putchar('\a');
                fflush(stdout);
            } else if (n == 1) {
                replace_token(buf, &len, &cursor, token_start, token_len, matches[0]);
                if (is_cmd && cursor == len && len < LINE_SIZE - 1) {
                    buf[len++] = ' ';
                    buf[len] = 0;
                    cursor++;
                }
                redraw_line(prompt, buf, cursor);
            } else {
                putchar('\n');
                for (int i = 0; i < n; i++) {
                    fputs(matches[i], stdout);
                    fputs("  ", stdout);
                }
                fputs("\n", stdout);
                fflush(stdout);
                redraw_line(prompt, buf, cursor);
            }
            continue;   
        }

        if (ch == 0x1b) {
            char seq[2];
            if (read(0, &seq[0], 1) <= 0) continue;
            if (read(0, &seq[1], 1) <= 0) continue;

            if (seq[0] == '[') {
                switch (seq[1]) {
                    case 'A':
                        if (history_idx > 0) {
                            if (!in_history) {
                                copy_str(saved_line, buf, LINE_SIZE);
                                saved_len = len;
                                in_history = 1;
                            }
                            history_idx--;
                            copy_str(buf, history[history_idx], max_len);
                            len = strlen_(buf);
                            cursor = len;
                            redraw_line(prompt, buf, cursor);
                        }
                        break;
                    case 'B':
                        if (history_idx < history_count) {
                            history_idx++;
                            if (history_idx == history_count) {
                                copy_str(buf, saved_line, max_len);
                                len = saved_len;
                                cursor = len;
                                in_history = 0;
                            } else {
                                copy_str(buf, history[history_idx], max_len);
                                len = strlen_(buf);
                                cursor = len;
                            }
                            redraw_line(prompt, buf, cursor);
                        }
                        break;
                    case 'C':
                        if (cursor < len) {
                            putchar(buf[cursor]);
                            fflush(stdout);
                            cursor++;
                        }
                        break;
                    case 'D':
                        if (cursor > 0) {
                            putchar('\b');
                            fflush(stdout);
                            cursor--;
                        }
                        break;
                }
            }
            continue;
        }

        if (ch == 8 || ch == 127) {
            if (cursor > 0) {
                for (int i = cursor - 1; i < len - 1; i++) {
                    buf[i] = buf[i + 1];
                }
                len--;
                buf[len] = 0;
                cursor--;
                putchar('\b');
                for (int i = cursor; i < len; i++) {
                    putchar(buf[i]);
                }
                putchar(' ');
                for (int i = 0; i < len - cursor + 1; i++) {
                    putchar('\b');
                }
                fflush(stdout);
            }
            continue;
        }

        if (len < max_len - 1) {
            for (int i = len; i > cursor; i--) {
                buf[i] = buf[i - 1];
            }
            buf[cursor] = ch;
            len++;
            buf[len] = 0;
            for (int i = cursor; i < len; i++) {
                putchar(buf[i]);
            }
            for (int i = 0; i < len - cursor - 1; i++) {
                putchar('\b');
            }
            fflush(stdout);
            cursor++;
        }
    }

    buf[len] = 0;

    if (len > 0) {
        int dup = 0;
        if (history_count > 0 && streq(history[history_count - 1], buf)) {
            dup = 1;
        }
        if (!dup) {
            if (history_count < HISTORY_MAX) {
                copy_str(history[history_count], buf, LINE_SIZE);
                history_count++;
            } else {
                for (int i = 0; i < HISTORY_MAX - 1; i++) {
                    copy_str(history[i], history[i + 1], LINE_SIZE);
                }
                copy_str(history[HISTORY_MAX - 1], buf, LINE_SIZE);
            }
        }
    }
    history_idx = history_count;

    return len;
}

/* ---- parse_args with quoted tracking ---- */
static int parse_args(char *line, char *argv[], int max_argc, int quoted[]) {
    int argc = 0;
    int i = 0;

    while (line[i]) {
        while (line[i] == ' ' || line[i] == '\t') i++;
        if (!line[i]) break;
        if (line[i] == '#') break;
        /* 超限必须报错，不能 break：静默截断会让命令带着一半参数被执行，
         * 症状是"参数明明给了却没生效"，而错误信息一个字都没有。 */
        if (argc >= max_argc) return -1;

        argv[argc] = &line[i];
        int w = i;
        int quote_type = 0;   /* 0=无引号, 1=单引号, 2=双引号 */

        while (line[i] && line[i] != ' ' && line[i] != '\t') {
            char c = line[i];
            if (c == '\\') {
                i++;
                if (line[i]) {
                    line[w++] = line[i];
                    i++;
                }
            } else if (c == '"') {
                quote_type = 2;
                i++;
                while (line[i] && line[i] != '"') {
                    if (line[i] == '\\' && line[i+1]) {
                        char nx = line[i+1];
                        if (nx == '$' || nx == '`' || nx == '"' || nx == '\\') {
                            line[w++] = nx;
                            i += 2;
                        } else if (nx == '\n') {
                            i += 2;             /* 续行：连反斜杠带换行一起删掉 */
                        } else {
                            line[w++] = '\\';
                            i++;
                        }
                    } else {
                        line[w++] = line[i++];
                    }
                }
                if (line[i] == '"') i++;
            } else if (c == '\'') {
                quote_type = 1;
                i++;
                while (line[i] && line[i] != '\'') {
                    line[w++] = line[i++];
                }
                if (line[i] == '\'') i++;
            } else {
                line[w++] = c;
                i++;
            }
        }
        int had_sep = (line[i] == ' ' || line[i] == '\t');
        line[w] = '\0';
        if (had_sep) i++;
        quoted[argc] = quote_type;
        argc++;
    }
    return argc;
}

/* ---- builtins ---- */
static void print_help(void) {
    fputs("commands:\n", stdout);
    fputs("  help\n", stdout);
    fputs("  exit\n", stdout);
    fputs("  pwd\n", stdout);
    fputs("  cd <path>\n", stdout);
    fputs("  mkdir <path>\n", stdout);
    fputs("  touch <path>\n", stdout);
    fputs("  rm [-r] <path>\n", stdout);
    fputs("  rmdir <path>\n", stdout);
    fputs("  whoami              (print effective user name)\n", stdout);
    fputs("  chmod <mode> <path> (mode is octal, e.g. 755 / 0644)\n", stdout);
    fputs("  chown <uid> <gid> <path>  (root only)\n", stdout);
    fputs("  rename <old> <new> (mv <old> <new> also works; ...)\n",stdout);
    fputs("  jobs                (list background / stopped jobs)\n", stdout);
    fputs("  fg [%n]             (bring job to foreground, continue if stopped)\n", stdout);
    fputs("  bg [%n]             (continue a stopped job in background)\n", stdout);
    fputs("  clear\n", stdout);
    fputs("  export NAME=VALUE   (set env var; $VAR / ${VAR} / $? expands in commands)\n", stdout);
    fputs("  env                 (print all env vars)\n", stdout);
    fputs("  unset NAME          (remove env var)\n", stdout);
    fputs("  source <script>     (run shell commands from a file)\n", stdout);
    fputs("  variable expansion: $VAR, ${VAR}, $? (last exit code)\n", stdout);
    fputs("    'single quotes' protect $; \"double quotes\" still expand $ (bash style)\n", stdout);
    fputs("  shutdown\n", stdout);
    fputs("\nexternal commands are in $PATH (/bin, /tests, /programs):\n", stdout);
    fputs("  filesystem: ls, cat, echo, grep, time\n", stdout);
    fputs("  network:     nslookup <host> [host ...], wget <url> [out], ping <host>\n", stdout);
    fputs("               ntpdate [server] [port], tftp <remote> [local], httpd [file]\n", stdout);
    fputs("               getip, setip <a.b.c.d>\n", stdout);
    fputs("  accounts:    id, su [name], passwd <name>\n", stdout);
    fputs("               useradd <name> [uid] [gid], groupadd <name> [gid]  (root only)\n", stdout);
    fputs("               usermod -aG <group> <user> | -g <gid> <user>       (root only)\n", stdout);
    fputs("               账户库 /var/etc/{passwd,group} 优先, 回退 /etc/{passwd,group}\n", stdout);
    fputs("  storage:     data disk mounted at /data; /home & /var are bind mounts of it\n", stdout);
    fputs("  try: ls /bin\n\n", stdout);
}

static int builtin_pwd(void) {
    char buf[128];
    if (getcwd(buf, sizeof(buf)) == NULL) {
        fputs("pwd: getcwd failed\n", stdout);
        return 1;
    }
    fputs(buf, stdout);
    fputs("\n", stdout);
    return 0;
}

static int builtin_cd(int argc, char *argv[]) {
    const char *path = "/";
    if (argc >= 2) path = argv[1];
    if (chdir(path) < 0) {
        fputs("cd: no such directory: ", stdout);
        fputs(path, stdout);
        fputs("\n", stdout);
        return 1;
    }
    /* 让 $PWD 反映新工作目录 */
    char cwd_buf[128];
    if (getcwd(cwd_buf, sizeof(cwd_buf)) != NULL) {
        setenv_s("PWD", cwd_buf, 1);
    }
    return 0;
}

static int builtin_mkdir(int argc, char *argv[]) {
    if (argc < 2) {
        fputs("mkdir: missing operand\n", stdout);
        return 1;
    }
    if (mkdir(argv[1], 0777) < 0) {
        fputs("mkdir: cannot create directory: ", stdout);
        fputs(argv[1], stdout);
        fputs("\n", stdout);
        return 1;
    }
    return 0;
}

static int builtin_create(int argc, char *argv[]) {
    if (argc < 2) {
        fputs("create: missing operand\n", stdout);
        return 1;
    }
    if (create(argv[1]) < 0) {
        fputs("create: cannot create file: ", stdout);
        fputs(argv[1], stdout);
        fputs("\n", stdout);
        return 1;
    }
    return 0;
}

static int builtin_rm(int argc, char *argv[]) {
    if (argc < 2) {
        fputs("rm: missing operand\n", stdout);
        return 1;
    }
    int recursive = 0;
    int start = 1;
    if (streq(argv[1], "-r") || streq(argv[1], "-rf") || streq(argv[1], "-f")) {
        if (streq(argv[1], "-r") || streq(argv[1], "-rf")) recursive = 1;
        start = 2;
    }
    int ret = 0;
    for (int i = start; i < argc; i++) {
        int r = recursive ? remove_recursive(argv[i]) : unlink(argv[i]);
        if (r < 0) {
            fputs("rm: cannot remove ", stdout);
            fputs(argv[i], stdout);
            fputs("\n", stdout);
            ret = 1;
        }
    }
    return ret;
}

static int builtin_rmdir(int argc, char *argv[]) {
    if (argc < 2) {
        fputs("rmdir: missing operand\n", stdout);
        return 1;
    }
    int ret = 0;
    for (int i = 1; i < argc; i++) {
        if (rmdir(argv[i]) < 0) {
            fputs("rmdir: failed to remove ", stdout);
            fputs(argv[i], stdout);
            fputs("\n", stdout);
            ret = 1;
        }
    }
    return ret;
}

/* uid -> 用户名(仅教学用的最小映射; 与 login/su 用户表保持一致) */
static const char *uid_to_name(usize uid) {
    if (uid == 0)   return "root";
    if (uid == 100) return "alice";
    if (uid == 101) return "bob";
    return NULL;
}

/* 打印无符号整数(保持与现有 builtin 一致用 fputc/fputs) */
static void fmt_uint(usize v) {
    char tmp[12]; int i = 0;
    if (v == 0) tmp[i++] = '0';
    while (v) { tmp[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i > 0) fputc(tmp[--i], stdout);
}

static int builtin_whoami(void) {
    usize e = (usize) geteuid();
    const char *n = uid_to_name(e);
    if (n) fputs(n, stdout);
    else { fputs("uid=", stdout); fmt_uint(e); }
    fputc('\n', stdout);
    return 0;
}

/* chmod <mode> <path>：mode 为八进制字符串(如 755 / 0644) */
static int builtin_chmod(int argc, char *argv[]) {
    if (argc < 3) {
        fputs("chmod: usage: chmod <mode> <path>\n", stdout);
        return 1;
    }
    const char *m = argv[1];
    usize mode = 0;
    for (int i = 0; m[i]; i++) {
        if (m[i] < '0' || m[i] > '7') {
            fputs("chmod: invalid mode (need octal digits 0-7)\n", stdout);
            return 1;
        }
        mode = (mode << 3) | (usize)(m[i] - '0');
    }
    if (chmod(argv[2], mode) < 0) {
        fputs("chmod: permission denied or no such file\n", stdout);
        return 1;
    }
    return 0;
}

/* chown <uid> <gid> <path>：仅 root 可改属主(内核已强制 euid==0) */
static int builtin_chown(int argc, char *argv[]) {
    if (argc < 4) {
        fputs("chown: usage: chown <uid> <gid> <path>\n", stdout);
        return 1;
    }
    usize uid = (usize) atoi(argv[1]);
    usize gid = (usize) atoi(argv[2]);
    if (chown(argv[3], uid, gid) < 0) {
        fputs("chown: permission denied or no such file\n", stdout);
        return 1;
    }
    return 0;
}

static int builtin_rename(int argc, char *argv[]) {
    if (argc < 3) {
        fputs("rename: usage: rename <oldpath> <newpath>\n", stdout);
        return 1;
    }
    if (rename(argv[1], argv[2]) < 0) {
        fputs("rename: failed: ", stdout);
        fputs(argv[1], stdout);
        fputs(" -> ", stdout);
        fputs(argv[2], stdout);
        fputs("\n", stdout);
        return 1;
    }
    return 0;
}

/* ---- 作业控制 ---- */
static int builtin_fg(int argc, char *argv[]) {
    struct job *j = find_job(argc > 1 ? argv[1] : 0);
    if (!j) { fputs("fg: no such job\n", stdout); return 1; }

    printf("%s\n", j->cmd);
    j->stopped = 0;
    set_front(j->pgid);              /* 交还前台：Ctrl+C / Ctrl+Z 重新打到整组 */
    kill(-j->pgid, SIGCONT);

    /* 等整组：管线里的成员会陆续退出，只有"组里再没有子进程"才算结束；
     * 任何一个成员被停住就整体算停住（POSIX 的作业停止就是这个语义）。 */
    int status = 0, stop_sig = 0, stopped = 0;
    isize r;
    for (;;) {
        r = waitpid(-j->pgid, &status, WUNTRACED);
        if (r < 0) {
            /* EINTR（被信号打断）= 什么都没发生，重试；其余负数才是"没孩子了" */
            if (errno == EINTR) continue;
            break;
        }
        if (WIFSTOPPED(status)) {
            stopped = 1;
            stop_sig = WSTOPSIG(status);
            break;
        }
    }

    set_front(getpgrp());            /* 前台收回 shell 自己所在的组 */
    set_echo(0);

    if (stopped) {                   /* 又按了一次 Ctrl+Z */
        j->stopped = 1;
        j->why = stop_sig;
        printf("\n[%d]+  Stopped  %s\n", j->id, j->cmd);
        return 128 + stop_sig;
    }

    printf("[%d] done %s\n", j->id, j->cmd);
    j->used = 0;
    /* 循环退出时 status 是最后一个成员的状态（正常退出/被信号杀） */
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

static int builtin_bg(int argc, char *argv[]) {
    struct job *j = find_job(argc > 1 ? argv[1] : 0);
    if (!j) { fputs("bg: no such job\n", stdout); return 1; }

    j->stopped = 0;
    kill(-j->pgid, SIGCONT);
    printf("[%d]+ %s &\n", j->id, j->cmd);

    /* 后台作业不占前台（和 `cmd &` 的处理一致） */
    set_front(getpgrp());
    return 0;
}

static void builtin_clear(void) {
    fputs("\x1b[2J\x1b[H", stdout);
    fflush(stdout);
}

/* export NAME=VALUE：把变量写入内核每进程环境（fork 继承、exec 作为 envp 传子进程）。
 * 不带 '=' 的 export NAME 视为无操作（本内核环境始终在内核侧维护）。 */
static int builtin_export(int argc, char *argv[]) {
    if (argc < 2) return 0;
    for (int i = 1; i < argc; i++) {
        char *eq = 0;
        for (int k = 0; argv[i][k]; k++) {
            if (argv[i][k] == '=') { eq = &argv[i][k]; break; }
        }
        if (!eq) continue;          /* export NAME（无值）：跳过 */
        *eq = 0;
        char *name = argv[i];
        char *val = eq + 1;
        setenv_s(name, val, 1);
    }
    return 0;
}

/* env：枚举并逐行打印 KEY=VALUE。 */
static int builtin_env(int argc, char *argv[]) {
    char buf[8192];
    buf[0] = 0;
    isize total = listenv(0, 0);          /* 先探测所需大小 */
    if (total < 0) return 0;
    if (total > (isize)(sizeof(buf) - 1)) {
        fputs("env: environment too large to print\n", stdout);
        return 0;
    }
    listenv(buf, sizeof(buf));
    int i = 0;
    while (i < total) {
        fputs(&buf[i], stdout);
        fputs("\n", stdout);
        i += strlen_(&buf[i]) + 1;
    }
    return 0;
}

/* unset NAME [NAME ...]：从内核环境删除变量。 */
static int builtin_unset(int argc, char *argv[]) {
    if (argc < 2) {
        fputs("unset: missing operand\n", stdout);
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        unsetenv_s(argv[i]);
    }
    return 0;
}

static void load_search_dirs(void) {
    num_dirs = 0;

    /* 命令搜索路径来自内核环境变量 PATH（由 init 播种为
     * /bin:/tests:/programs，可用 export PATH=... 改写）。
     * 不再依赖 /etc/path 文件。 */
    const char *path = getenv("PATH");
    if (!path || path[0] == 0) {
        copy_str(search_dirs[num_dirs++], "/bin/", DIR_LEN);
        copy_str(search_dirs[num_dirs++], "/tests/", DIR_LEN);
        if (num_dirs < MAX_DIRS)
            copy_str(search_dirs[num_dirs++], "/programs/", DIR_LEN);
        return;
    }

    char buf[256];
    int plen = 0;
    for (int i = 0; path[i] && plen < (int)sizeof(buf) - 1; i++)
        buf[plen++] = path[i];
    buf[plen] = 0;

    int start = 0;
    for (int i = 0; i <= plen; i++) {
        if (buf[i] == ':' || buf[i] == 0) {
            int len = i - start;
            if (len > 0 && num_dirs < MAX_DIRS) {
                int k = 0;
                if (len > DIR_LEN - 2) len = DIR_LEN - 2;
                for (int j = start; j < start + len; j++) {
                    search_dirs[num_dirs][k++] = buf[j];
                }
                if (k > 0 && search_dirs[num_dirs][k - 1] != '/') {
                    search_dirs[num_dirs][k++] = '/';
                }
                search_dirs[num_dirs][k] = '\0';
                num_dirs++;
            }
            start = i + 1;
        }
    }
    if (num_dirs == 0) {
        copy_str(search_dirs[num_dirs++], "/bin/", DIR_LEN);
        copy_str(search_dirs[num_dirs++], "/tests/", DIR_LEN);
    }
}

static void build_exec_path(const char *prefix, const char *cmd, char *out, int out_size) {
    if (cmd[0] == '/') {
        int i = 0;
        while (cmd[i] && i < out_size - 1) {
            out[i] = cmd[i];
            i++;
        }
        out[i] = 0;
        return;
    }
    int pos = 0;
    for (int i = 0; prefix[i] && pos < out_size - 1; i++) {
        out[pos++] = prefix[i];
    }
    for (int i = 0; cmd[i] && pos < out_size - 1; i++) {
        out[pos++] = cmd[i];
    }
    out[pos] = 0;
}



/* exec_with_args 成功不返回；返回就是失败，值是内核原生的 -errno
 * （不带 rk_sys_ret 的 -1 语义）。候选路径里"后面的目录不存在"是正常的，
 * 真正的原因在第一个非 ENOENT 的错误上，所以要记第一个而不是最后一个。 */
static void report_exec_failure(const char *cmd, int err) {
    if (err == 0 || err == ENOENT) {
        printf("command not found: %s\n", cmd);
    } else {
        printf("cannot execute %s: %s (errno %d)\n", cmd, strerror(err), err);
    }
}

static void exec_try(const char *path, struct exec_args *args, int *err) {
    isize r = exec_with_args(path, args);
    if (r < 0) {
        int e = (int)(-r);
        if (*err == 0 || (*err == ENOENT && e != ENOENT)) *err = e;
    }
}

static void run_exec(int argc, char *argv[]){
    struct exec_args args;
    int err = 0;
    args.argc = argc;

    for (int i = 0; i < EXEC_MAX_ARGS; i++) {
        args.argv[i].ptr = 0;
        args.argv[i].len = 0;
    }
    for (int i = 0; i < argc && i < EXEC_MAX_ARGS; i++) {
        args.argv[i].ptr = argv[i];
        args.argv[i].len = strlen_(argv[i]);
    }

    if (has_slash(argv[0])) {
        exec_try(argv[0], &args, &err);
        report_exec_failure(argv[0], err);
        return;
    }

    exec_try(argv[0], &args, &err);

    for (int d = 0; d < num_dirs; d++) {
        char path[96];
        build_exec_path(search_dirs[d], argv[0], path, sizeof(path));
        exec_try(path, &args, &err);
    }

    report_exec_failure(argv[0], err);
}

static int run_external(int argc, char *argv[], int background) {
    /* Refresh PATH each time before running an external command. */
    load_search_dirs();
    /* 外部命令(尤其 sqlite3/cat 这类交互程序)依赖内核回显: 打开它。
     * 本 shell 自带行编辑器, read_line() 时已关掉, 这里重新打开。 */
    set_echo(1);
    isize pid = fork();
    if (pid == 0) {
        /* 自成进程组：这样 Ctrl+C / Ctrl+Z 按组投递时只打到作业本身，
         * 不会连 shell 一起打（shell 和前台作业必须在不同的组）。
         * 父子两边都调一次是 bash 的做法：谁先跑到都不会漏。 */
        setpgid(0, 0);
        /* Child stdin must stay BLOCKING. The old code set O_NONBLOCK on
         fd 0 before fork for Ctrl+C polling, and fd_flags get cloned
         into the child, so interactive programs (sqlite3 shell, cat,
         lua...) saw read()==0 (EOF) on stdin and exited immediately.
         Reset here; per-process fd_flags so this does not affect the
         parent. */
        fcntl(0, F_SETFL, 0);
        run_exec(argc, argv);
        exit(1);
    } else if (pid > 0) {
        setpgid(pid, pid);        /* 与子进程那侧配对，消除竞态 */
        if (background) {
            add_job(pid, argv[0], 0);
            printf("[%d] %d\n", next_job_id - 1, pid);
            set_echo(0);            /* 回 shell 提示符, 关回显 */
            set_front(getpgrp());   /* 后台任务不占前台: Ctrl+C 回 shell */
            return 0;
        }
        /* Foreground: block until the child exits. We no longer poll
         read(0) for Ctrl+C: that consumed input chars meant for the
         child from the shared terminal. Kernel signal delivery is
         incomplete; Ctrl+C interrupt can come back with a kernel
         Ctrl+C -> SIGINT implementation later. */
        /* 把前台交给子进程：Ctrl+C / Ctrl+Z 的投递目标是"前台进程"，
         * 而 fork 不会自动改它 —— 不写这句，信号永远打在 shell 自己身上
         * （shell 又忽略了 SIGINT/SIGTSTP，于是按键毫无反应）。 */
        set_front(pid);

        /* WUNTRACED：Ctrl+Z 会让 waitpid 带着"停住"回来，而不是一直等。
         * 不传它的话，被停住的前台进程会让 shell 一起卡死（谁也动不了）。 */
        int status = 0;
        while (waitpid(pid, &status, WUNTRACED) < 0) {
            if (errno != EINTR) { status = 0; break; }   /* 没这个孩子了 */
        }
        set_echo(0);   /* 回 shell 提示符, 关回显 */

        if (WIFSTOPPED(status)) {
            /* 停住的作业要进作业表，否则 fg/bg 找不到它。
             * 前台也必须收回自己 —— 否则之后按 Ctrl+C 还会打到那个停止的进程。 */
            int jid = add_job(pid, argv[0], 1);
            printf("\n[%d]+  Stopped  %s\n", jid, argv[0]);
            set_front(getpgrp());
            return 128 + WSTOPSIG(status);
        }

        /* $? 的约定（与 bash 一致）：正常退出取退出码，被信号杀死取 128+sig。
         * 内核给的是 POSIX wait status，"压平"这一步只发生在 shell 这里 ——
         * 不能把压平后的值当成 waitpid 的返回值去理解。 */
        return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    } else {
        set_echo(0);
        fputs("fork failed\n", stdout);
        return 1;
    }
}

/* ---- 管道 & 重定向 ---- */
/* 一条重定向操作。按记录顺序左到右应用，这是 POSIX 的硬性规定，也是
 * `2>&1 >f` 和 `>f 2>&1` 结果不同的唯一来源：
 *   2>&1 >f   stderr 复制的是重定向前的 stdout（通常是终端），stdout 去 f
 *   >f 2>&1   stdout 先去 f，stderr 再复制 stdout → 两个都进 f
 * 用"字段式"（outfile/errfile/err_to_out）表达不了这个差别，必须留序列。 */
struct redir_op {
    int  fd;           /* 被重定向的 fd：0/1/2 */
    int  dup_from;     /* >=0 表示 dup2(from, fd)；-1 表示打开 name */
    int  append;
    char name[256];
};

#define MAX_REDIRS 8
#define MAX_ASSIGNS 8

/* 前缀赋值 `VAR=v cmd` 的载体。name/val 都指向 argv 里的副本（就地切分），
 * 所以生命周期跟 argv 一样长，不需要额外拷贝。 */
struct assign_kv {
    char *name;
    char *val;
};

struct segment {
    char *cmd_str;
    struct redir_op ops[MAX_REDIRS];
    int  nops;
    struct assign_kv assigns[MAX_ASSIGNS];
    int  nassigns;
};

/* `NAME=VALUE` 且 NAME 是合法标识符。注意只认词首：`a=b` 是赋值，
 * `./a=b` 和 `x/a=b` 是文件名。 */
static int is_assign_word(const char *s) {
    if (!s || !*s) return 0;
    if (!((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z') || *s == '_'))
        return 0;
    const char *p = s + 1;
    while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
           (*p >= '0' && *p <= '9') || *p == '_') p++;
    return *p == '=';
}

/* 就地切分成两段，返回 name，val 由出参带回。调用方必须保证 s 可写。 */
static char *split_assign(char *s, char **val) {
    char *eq = s;
    while (*eq && *eq != '=') eq++;
    *eq = 0;
    *val = eq + 1;
    return s;
}

static char *skip_spaces(char *p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

/* 把 "2>" 里的那个 "2" 从已写入的 cmd_str 里抹掉，并返回 fd 编号。
 * 必须紧邻且在词首：`a1>f` 里的 1 是文件名的一部分（a1），不是 fd。 */
static int eat_fd_prefix(char *buf, int *w, int dflt) {
    int b = *w;
    while (b > 0 && buf[b - 1] >= '0' && buf[b - 1] <= '9') b--;
    if (b == *w) return dflt;
    if (b > 0 && buf[b - 1] != ' ' && buf[b - 1] != '\t') return dflt;
    int v = 0;
    for (int q = b; q < *w; q++) v = v * 10 + (buf[q] - '0');
    if (v < 0 || v > 2) return dflt;   /* 只支持 0/1/2，别的当普通数字 */
    *w = b;
    return v;
}

static int parse_redirect(char *seg_str, struct segment *out) {
    out->cmd_str = seg_str;
    out->nops    = 0;
    out->nassigns = 0;

    int i = 0;
    int w = 0;
    while (seg_str[i]) {
        char c = seg_str[i];
        if (c == '"' || c == '\'') {
            char quote = c;
            seg_str[w++] = seg_str[i++];
            while (seg_str[i] && seg_str[i] != quote) {
                if (quote == '"' && seg_str[i] == '\\' && seg_str[i+1]) {
                    seg_str[w++] = seg_str[i++];
                }
                seg_str[w++] = seg_str[i++];
            }
            if (seg_str[i] == quote) {
                seg_str[w++] = seg_str[i++];
            }
            continue;
        }
        if (c == '\\') {
            seg_str[w++] = seg_str[i++];
            if (seg_str[i]) seg_str[w++] = seg_str[i++];
            continue;
        }
        if (c == '>' || c == '<') {
            int is_output = (c == '>');
            int fd = eat_fd_prefix(seg_str, &w, is_output ? 1 : 0);
            int append = 0;
            i++;
            if (is_output && seg_str[i] == '>') {
                append = 1;
                i++;
            }

            if (out->nops >= MAX_REDIRS) return -1;

            /* >&N / <&N：fd 复制。这是 `2>&1` 的落点 —— 之前 `&` 在
             * scan_seps 里就被当成后台符号切掉了，压根走不到这里。 */
            if (seg_str[i] == '&') {
                i++;
                if (seg_str[i] < '0' || seg_str[i] > '9') return -1;
                int src = 0;
                while (seg_str[i] >= '0' && seg_str[i] <= '9') {
                    src = src * 10 + (seg_str[i] - '0');
                    i++;
                }
                if (src < 0 || src > 2) return -1;
                struct redir_op *op = &out->ops[out->nops++];
                op->fd = fd;
                op->dup_from = src;
                op->append = 0;
                op->name[0] = 0;
                continue;
            }

            i = (int)(skip_spaces(seg_str + i) - seg_str);
            if (seg_str[i] == 0 ||
                seg_str[i] == '>' || seg_str[i] == '<' || seg_str[i] == '|') {
                return -1;
            }
            struct redir_op *op = &out->ops[out->nops++];
            op->fd = fd;
            op->dup_from = -1;
            op->append = append;
            int t = 0;
            while (seg_str[i] && seg_str[i] != ' ' && seg_str[i] != '\t' &&
                   seg_str[i] != '>' && seg_str[i] != '<' && seg_str[i] != '|') {
                if (t < (int)sizeof(op->name) - 1) op->name[t++] = seg_str[i];
                i++;
            }
            op->name[t] = 0;
            if (t == 0) return -1;
            continue;
        }
        seg_str[w++] = seg_str[i++];
    }
    seg_str[w] = 0;
    return 0;
}

/* 按记录顺序应用重定向。返回 -1 表示打开失败（调用方决定怎么退出）。
 * 拆出来是为了能在宿主上直接验证 fd 指向 —— 这段逻辑的正确性完全取决于
 * "顺序"，只看代码很容易看错。 */
static int apply_redirects(struct segment *seg) {
    for (int k = 0; k < seg->nops; k++) {
        struct redir_op *op = &seg->ops[k];

        if (op->dup_from >= 0) {
            if (op->dup_from != op->fd && dup2(op->dup_from, op->fd) < 0) {
                printf("cannot dup fd %d -> %d\n", op->dup_from, op->fd);
                return -1;
            }
            continue;
        }

        int flags;
        if (op->fd == 0)
            flags = O_RDONLY;
        else if (op->append)
            flags = O_CREAT | O_APPEND | O_WRONLY;
        else
            flags = O_CREAT | O_TRUNC | O_WRONLY;

        isize fd = open(op->name, flags, 0644);
        if (fd < 0) {
            printf("cannot open %s\n", op->name);
            return -1;
        }
        if (fd != op->fd) {
            dup2(fd, op->fd);
            close(fd);
        }
    }
    return 0;
}

static void apply_redirect_and_exec(struct segment *seg, int argc, char *argv[]) {
    /* 前缀赋值只活在这个子进程里，exec 之前设好就行 —— 这正是
     * `VAR=v cmd | other` 里赋值不泄漏给 other 的原因。 */
    for (int k = 0; k < seg->nassigns; k++)
        setenv_s(seg->assigns[k].name, seg->assigns[k].val, 1);

    /* 严格按解析顺序应用：`2>&1 >f` 的 stderr 指向**重定向前**的 stdout，
     * `>f 2>&1` 的两个都进 f。位置一换语义就变，所以这里不能分组优化。 */
    if (apply_redirects(seg) < 0) exit(1);
    run_exec(argc, argv);
    exit(1);
}

static int run_pipeline(char *line) {
    char *seg_strs[MAX_SEGMENTS];
    int   nseg = 0;
    seg_strs[nseg++] = line;
    {
        char quote = 0;
        for (int i = 0; line[i]; i++) {
            char c = line[i];
            if (quote) {
                if (c == '\\' && quote == '"' && line[i+1]) {
                    i++;
                } else if (c == quote) {
                    quote = 0;
                }
                continue;
            }
            if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == '\\' && line[i+1]) {
                i++;
            } else if (c == '#' && (i == 0 || line[i-1] == ' ' || line[i-1] == '\t')) {
                line[i] = 0;
                break;
            } else if (c == '|') {
                line[i] = 0;
                if (nseg >= MAX_SEGMENTS) {
                    fputs("too many pipe segments\n", stdout);
                    return 1;
                }
                seg_strs[nseg++] = line + i + 1;
            }
        }
    }

    struct segment segs[MAX_SEGMENTS];
    for (int s = 0; s < nseg; s++) {
        if (parse_redirect(seg_strs[s], &segs[s]) < 0) {
            fputs("syntax error in redirection\n", stdout);
            return 1;
        }
    }

    static char *seg_argv[MAX_SEGMENTS][MAX_ARGC];
    static char pipeline_arg_strs[MAX_SEGMENTS][MAX_ARGC][LINE_SIZE];
    int seg_argc[MAX_SEGMENTS];
    int seg_quoted[MAX_SEGMENTS][MAX_ARGC];
    for (int s = 0; s < nseg; s++) {
        int q0[MAX_ARGC];
        char *a0[MAX_ARGC];
        int ac0 = parse_args(segs[s].cmd_str, a0, MAX_ARGC, q0);
        if (ac0 < 0) {
            printf("shell: too many arguments (max %d)\n", MAX_ARGC);
            return 1;
        }
        if (ac0 == 0) {
            fputs("syntax error: empty command\n", stdout);
            return 1;
        }
        /* 变量展开 + brace/glob 展开（与 run_node 同处理） */
        char *ve[MAX_ARGC];
        int vq[MAX_ARGC];
        int vac = expand_variables(ac0, a0, q0, ve, vq);
        char *ea[MAX_ARGC];
        int eac = expand_args(vac, ve, ea, vq);
        seg_argc[s] = eac;
        /* expand_args 写入共享 glob_pool，多段解析循环会覆盖它；
         * 故把每个 token 拷到按段独立的池，避免后续段破坏前段参数。 */
        for (int k = 0; k < eac && k < MAX_ARGC; k++) {
            copy_str(pipeline_arg_strs[s][k], ea[k], LINE_SIZE);
            seg_argv[s][k] = pipeline_arg_strs[s][k];
        }
        for (int k = 0; k < eac && k < MAX_ARGC; k++) seg_quoted[s][k] = vq[k];

        /* 前缀赋值 `VAR=v cmd | ...`：赋值只作用于**本段**。
         * 切的是 pipeline_arg_strs 里的副本，就地写 \0 不会影响别的段。 */
        int np = 0;
        while (np < eac && is_assign_word(seg_argv[s][np])) np++;
        if (np > MAX_ASSIGNS) {
            printf("shell: too many assignments (max %d)\n", MAX_ASSIGNS);
            return 1;
        }
        for (int k = 0; k < np; k++) {
            char *av;
            segs[s].assigns[k].name = split_assign(seg_argv[s][k], &av);
            segs[s].assigns[k].val  = av;
        }
        segs[s].nassigns = np;
        if (np > 0) {
            if (eac == np) {
                fputs("syntax error: empty command\n", stdout);
                return 1;
            }
            for (int k = 0; k < eac - np; k++) seg_argv[s][k] = seg_argv[s][np + k];
            seg_argc[s] = eac - np;
        }
    }

    int prev_read = -1;
    int pids[MAX_SEGMENTS];
    int npid = 0;

    for (int s = 0; s < nseg; s++) {
        int pipefd[2];
        int has_next = (s < nseg - 1);
        if (has_next) {
            if (pipe(pipefd) < 0) {
                fputs("pipe failed\n", stdout);
                break;
            }
        }
        int pid = fork();
        if (pid == 0) {
            /* 整条管线一个进程组：第一段当组长，后面几段加入它。
             * 这样 Ctrl+C / Ctrl+Z 一次就能打到整条管线 —— 否则只停一个，
             * 剩下的会变成孤儿继续跑，还往已经没人读的管道里写。 */
            setpgid(0, npid == 0 ? 0 : pids[0]);
            if (prev_read >= 0) dup2(prev_read, 0);
            if (has_next) dup2(pipefd[1], 1);
            if (prev_read >= 0) close(prev_read);
            if (has_next) {
                close(pipefd[0]);
                close(pipefd[1]);
            }
            apply_redirect_and_exec(&segs[s], seg_argc[s], seg_argv[s]);
            exit(1);
        } else if (pid > 0) {
            setpgid(pid, npid == 0 ? pid : pids[0]);   /* 与子进程那侧配对 */
            pids[npid++] = pid;
        } else {
            fputs("fork failed\n", stdout);
        }
        if (prev_read >= 0) close(prev_read);
        if (has_next) {
            close(pipefd[1]);
            prev_read = pipefd[0];
        } else {
            prev_read = -1;
        }
    }
    if (prev_read >= 0) close(prev_read);

    /* 把整组交给前台：Ctrl+C / Ctrl+Z 的投递目标是"前台进程组" */
    int leader = npid > 0 ? pids[0] : 0;
    if (leader > 0) set_front(leader);

    int last_status = 0, stopped = 0, stop_sig = 0;
    for (int i = 0; i < npid; i++) {
        int status = 0;
        isize wr;
        do { wr = waitpid(pids[i], &status, WUNTRACED); } while (wr < 0 && errno == EINTR);
        if (wr < 0) continue;
        if (WIFSTOPPED(status)) {
            /* 组投递下整条管线会一起停；拿到第一个就够了 */
            stopped = 1;
            stop_sig = WSTOPSIG(status);
            break;
        }
        if (i == npid - 1) {
            /* 同上一处：$? = 退出码，或 128 + 信号号 */
            last_status = WIFEXITED(status) ? WEXITSTATUS(status)
                                            : 128 + WTERMSIG(status);
        }
    }

    if (leader > 0) set_front(getpgrp());

    if (stopped) {
        char how[LINE_SIZE];
        copy_str(how, segs[0].cmd_str, LINE_SIZE);
        int jid = add_job(leader, how, 1);
        printf("\n[%d]+  Stopped  %s\n", jid, how);
        return 128 + stop_sig;
    }
    return last_status;
}
static int has_pipe_or_redirect(const char *s) {
    char quote = 0;
    for (int i = 0; s[i]; i++) {
        if (quote) {
            if (s[i] == quote) quote = 0;
            continue;
        }
        if (s[i] == '"' || s[i] == '\'') {
            quote = s[i];
        } else if (s[i] == '|' || s[i] == '<' || s[i] == '>') {
            return 1;
        }
    }
    return 0;
}

static int builtin_source(const char *path);
static int execute_line(char *line);

/* 分派体：前缀赋值剥离后，真正按命令名执行。独立成函数是为了让
 * `VAR=v cmd` 能在**同一进程**里设置环境、递归调用、再恢复。 */
static int run_dispatch(int argc, char **argv, int background) {
    if (streq(argv[0], "help")) { print_help(); return 0; }
    if (streq(argv[0], "exit")) { fputs("bye\n", stdout); exit(0); }
    if (streq(argv[0], "export")) { return builtin_export(argc, argv); }
    if (streq(argv[0], "env")) { return builtin_env(argc, argv); }
    if (streq(argv[0], "unset")) { return builtin_unset(argc, argv); }
    if (streq(argv[0], "pwd")) { return builtin_pwd(); }
    if (streq(argv[0], "cd")) { return builtin_cd(argc, argv); }
    if (streq(argv[0], "mkdir")) { return builtin_mkdir(argc, argv); }
    if (streq(argv[0], "touch")) { return builtin_create(argc, argv); }
    if (streq(argv[0], "rm")) { return builtin_rm(argc, argv); }
    if (streq(argv[0], "rmdir")) { return builtin_rmdir(argc, argv); }
    if (streq(argv[0], "whoami")) { return builtin_whoami(); }
    if (streq(argv[0], "chmod")) { return builtin_chmod(argc, argv); }
    if (streq(argv[0], "chown")) { return builtin_chown(argc, argv); }
    if (streq(argv[0], "rename") || streq(argv[0], "mv")) { return builtin_rename(argc, argv); }
    if (streq(argv[0], "jobs")) { print_jobs(); return 0; }
    if (streq(argv[0], "fg")) { return builtin_fg(argc, argv); }
    if (streq(argv[0], "bg")) { return builtin_bg(argc, argv); }
    if (streq(argv[0], "clear")) { builtin_clear(); return 0; }
    if (streq(argv[0], "source") || streq(argv[0], ".")) {
        if (argc < 2) {
            fputs("source: missing file operand\n", stdout);
            return 1;
        }
        return builtin_source(argv[1]);
    }
    if (streq(argv[0], "shutdown")) { fputs("bye bye~\n", stdout); rk_shutdown(); return 0; }

    /* break / continue：**必须同进程**。走 fork 的话退出码被 wait 截成 8 位，
     * 300/301 变成 44/45，循环根本认不出来，症状是"break 执行了但一直在循环"。 */
    if (streq(argv[0], "break") || streq(argv[0], "continue")) {
        if (g_loop_depth <= 0) {
            fputs("shell: ", stdout);
            fputs(argv[0], stdout);
            fputs(": only meaningful in a loop\n", stdout);
            return 0;
        }
        return streq(argv[0], "break") ? CTL_BREAK : CTL_CONTINUE;
    }

    return run_external(argc, argv, background);
}

/* ---- 单命令执行（含内置命令、通配展开、后台） ---- */
static int run_node(char *cmd, int background) {
    if (has_pipe_or_redirect(cmd)) {
        return run_pipeline(cmd);
    }

    int quoted[MAX_ARGC];
    char *argv[MAX_ARGC];
    int argc = parse_args(cmd, argv, MAX_ARGC, quoted);
    if (argc < 0) {
        printf("shell: too many arguments (max %d)\n", MAX_ARGC);
        return 2;
    }
    if (argc == 0) return 0;

    /* 变量展开：$VAR / ${VAR}（引号内保护的 token 跳过）。
     * 必须在 brace/glob 展开之前，使 $VAR 的结果也能被通配。 */
    char *var_argv[MAX_ARGC];
    int var_quoted[MAX_ARGC];
    int var_argc = expand_variables(argc, argv, quoted, var_argv, var_quoted);

    if (var_argc > 0 && streq(var_argv[var_argc - 1], "&")) {
        background = 1;
        var_argc--;
    }

    char *exp_argv[MAX_ARGC];
    int exp_argc = expand_args(var_argc, var_argv, exp_argv, var_quoted);

    /* ---- 前缀赋值 `VAR=v cmd` 与裸赋值 `VAR=v` ---- */
    int npre = 0;
    while (npre < exp_argc && is_assign_word(exp_argv[npre])) npre++;
    if (npre > 0 && npre == exp_argc) {
        /* 整条只有赋值：永久写进环境，不执行命令（POSIX） */
        for (int i = 0; i < npre; i++) {
            char *v;
            char *n = split_assign(exp_argv[i], &v);
            setenv_s(n, v, 1);
        }
        return 0;
    }
    if (npre > MAX_ASSIGNS) {
        printf("shell: too many assignments (max %d)\n", MAX_ASSIGNS);
        return 2;
    }
    if (npre > 0) {
        /* 有命令：临时设置，执行完原样恢复。
         * getenv 返回的是静态缓冲，必须先拷走 —— 下一次 getenv 会覆盖它。 */
        char *names[MAX_ASSIGNS];
        char  oldv[MAX_ASSIGNS][256];
        int   had[MAX_ASSIGNS];
        for (int i = 0; i < npre; i++) {
            char *v;
            names[i] = split_assign(exp_argv[i], &v);
            const char *old = getenv(names[i]);
            had[i] = (old != 0);
            oldv[i][0] = 0;
            if (old) copy_str(oldv[i], old, sizeof(oldv[i]));
            setenv_s(names[i], v, 1);
        }
        int rc = run_dispatch(exp_argc - npre, exp_argv + npre, background);
        for (int i = 0; i < npre; i++) {
            if (had[i]) setenv_s(names[i], oldv[i], 1);
            else unsetenv_s(names[i]);
        }
        return rc;
    }

    return run_dispatch(exp_argc, exp_argv, background);
}

/* ---- 命令替换 $() / 反引号 ---- */
static int cmdsub_depth = 0;
#define CMDSUB_MAX_DEPTH 8

/* 执行子命令并捕获 stdout(经临时文件),返回捕获字符串(静态缓冲,已去尾部换行) */
static const char *capture_cmd(const char *cmd) {
    if (cmdsub_depth >= CMDSUB_MAX_DEPTH) return "";
    cmdsub_depth++;

    static char tmp[48];
    static char buf[1024];
    snprintf(tmp, sizeof tmp, "/tmp/.cmdsub%d", cmdsub_depth);

    isize pid = fork();
    if (pid == 0) {
        /* 子进程: stdout 重定向到临时文件后执行(内置+外部命令通吃) */
        int fd = open(tmp, O_CREAT | O_TRUNC | O_WRONLY, 0644);
        if (fd < 0) exit(1);
        dup2(fd, 1);
        close(fd);
        char cmd_copy[LINE_SIZE];
        copy_str(cmd_copy, cmd, LINE_SIZE);
        execute_line(cmd_copy);       /* 复用整行执行,支持 ; && || 与嵌套替换 */
        fflush(stdout);
        exit(0);
    } else if (pid > 0) {
        int status = 0;
        while (waitpid(pid, &status, 0) < 0) {
            if (errno != EINTR) break;
        }
    }

    buf[0] = 0;
    int fd = open(tmp, O_RDONLY);
    if (fd >= 0) {
        int n = read(fd, buf, sizeof buf - 1);
        if (n > 0) buf[n] = 0; else buf[0] = 0;
        close(fd);
    }
    unlink(tmp);

    /* POSIX 语义: 命令替换结果去掉尾部的所有换行 */
    int len = strlen_(buf);
    while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) buf[--len] = 0;

    cmdsub_depth--;
    return buf;
}

/* 处理单个命令替换: src[i] 指向 "$(" 或 "`",提取子命令执行,结果写入 out,返回新的源索引 */
static int do_cmdsub(char *src, int i, char *out, int *o, int dollar_paren) {
    int cmd_start = dollar_paren ? i + 2 : i + 1;
    int k = cmd_start;
    int depth = 1;
    char close = dollar_paren ? ')' : '`';
    while (src[k]) {
        if (dollar_paren && src[k] == '(') depth++;
        else if (src[k] == close) {
            depth--;
            if (depth == 0) break;
        }
        k++;
    }
    if (src[k] != close) {
        /* 没配对,原样输出该字符 */
        out[(*o)++] = src[i++];
        return i;
    }

    char cmd[LINE_SIZE];
    int cl = k - cmd_start;
    if (cl >= LINE_SIZE - 1) cl = LINE_SIZE - 1;
    for (int t = 0; t < cl; t++) cmd[t] = src[cmd_start + t];
    cmd[cl] = 0;

    const char *r = capture_cmd(cmd);
    for (int t = 0; r[t] && *o < LINE_SIZE - 1; t++) out[(*o)++] = r[t];
    return k + 1;   /* 跳过整个 $(...) / `...` */
}

/* 扫描整行,把 $(...) 与 `...` 替换为命令输出(单引号内不处理) */
static void expand_cmdsub(char *line) {
    char out[LINE_SIZE];
    int o = 0;
    int i = 0;
    int squote = 0;
    while (line[i] && o < LINE_SIZE - 1) {
        char c = line[i];
        if (squote) {
            out[o++] = line[i++];
            if (c == '\'') squote = 0;
            continue;
        }
        if (c == '\'') {
            squote = 1;
            out[o++] = line[i++];
            continue;
        }
        if (c == '$' && line[i+1] == '(') {
            i = do_cmdsub(line, i, out, &o, 1);
            continue;
        }
        if (c == '`') {
            i = do_cmdsub(line, i, out, &o, 0);
            continue;
        }
        out[o++] = line[i++];
    }
    out[o] = 0;
    copy_str(line, out, LINE_SIZE);
}

/* 复合命令执行层
 *
 * 分层（自下而上）：
 *   run_node     一条原子命令（内置 + 外部 + 管道 + 重定向）
 *   run_command  识别 if / for / while / until，否则转 run_node
 *   exec_and_or  按深度 0 的 && / || 串起来
 *   exec_list    按深度 0 的 ; / & 串起来          <- 递归点
 */

static char *skip_ws(char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n') p++;
    return p;
}

static int exec_and_or(char *stmt, int bg);
static int run_command(char *cmd, int bg);

/* 执行一个 list。复合命令的 body 也走这里，形成递归。 */
static int exec_list(char *line) {
    strip_comment(line);
    expand_cmdsub(line);

    struct sep seps[32];
    int n = scan_seps(line, seps, 32);

    int last = 0;
    int s = 0;
    for (int k = 0; k < n; k++) {
        /* && / || 属于下一层，这里只切 ; 和 & */
        if (seps[k].kind == SEP_AND || seps[k].kind == SEP_OR) continue;
        int bg = (seps[k].kind == SEP_BG);
        line[seps[k].pos] = 0;
        char *seg = skip_ws(line + s);
        if (*seg) {
            last = exec_and_or(seg, bg);
            if (last > CTL_MAX) return last;   /* break/continue 立刻上抛 */
        }
        s = seps[k].pos + seps[k].len;
    }
    char *tail = skip_ws(line + s);
    if (*tail) last = exec_and_or(tail, 0);
    return last;
}

/* 一段内按深度 0 的 && / || 串起来。整段只有一个分隔符时就是普通的一条命令。 */
static int exec_and_or(char *stmt, int bg) {
    struct sep seps[16];
    int n = scan_seps(stmt, seps, 16);

    int last = 0;
    int s = 0;
    int op = 0;
    for (int k = 0; k < n; k++) {
        if (seps[k].kind != SEP_AND && seps[k].kind != SEP_OR) continue;
        stmt[seps[k].pos] = 0;
        char *seg = skip_ws(stmt + s);
        int run = 1;
        if (op == SEP_AND && last != 0) run = 0;
        if (op == SEP_OR  && last == 0) run = 0;
        if (run && *seg) {
            last = run_command(seg, 0);
            if (last > CTL_MAX) return last;
            g_last_exit = last;
        }
        op = seps[k].kind;
        s = seps[k].pos + seps[k].len;
    }

    char *tail = skip_ws(stmt + s);
    int run = 1;
    if (op == SEP_AND && last != 0) run = 0;
    if (op == SEP_OR  && last == 0) run = 0;
    if (run && *tail) {
        last = run_command(tail, bg);
        if (last > CTL_MAX) return last;
        g_last_exit = last;
    }
    return last;
}

static int run_if(char *p) {
    for (int guard = 0; guard < 32; guard++) {
        int tids[1] = { KW_THEN };
        int id = -1;
        char *then = find_kw0(p, tids, 1, &id);
        if (!then) { fputs("shell: if: missing then\n", stdout); return 2; }
        *then = 0;

        int st = exec_list(p);              /* 条件（可含 && / || / 管道） */

        char *body = skip_ws(then + 4);
        int bids[3] = { KW_ELIF, KW_ELSE, KW_FI };
        int bid = -1;
        char *next = find_kw0(body, bids, 3, &bid);
        if (!next) { fputs("shell: if: missing fi\n", stdout); return 2; }
        *next = 0;

        if (st == 0) return exec_list(body);    /* break/continue 透传 */

        if (bid == KW_ELIF) { p = next + 4; continue; }   /* <- elif 的全部增量 */
        if (bid == KW_ELSE) {
            char *eb = skip_ws(next + 4);
            int fids[1] = { KW_FI };
            int fid = -1;
            char *fi = find_kw0(eb, fids, 1, &fid);
            if (!fi) { fputs("shell: if: missing fi\n", stdout); return 2; }
            *fi = 0;
            return exec_list(eb);
        }
        /* 没有 else 且条件为假：POSIX 规定 if 整体返回 0，不是条件的返回码。
         * 返回 st 会让 `if grep x f; then ...; fi; echo $?` 打印 1。 */
        return 0;
    }
    fputs("shell: if: too many elif\n", stdout);
    return 2;
}

static int run_for(char *p) {
    p = skip_ws(p);
    char *name = p;
    while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != ';') p++;
    if (p == name) { fputs("shell: for: missing loop variable\n", stdout); return 2; }

    char vname[64];
    int nl = (int)(p - name);
    if (nl > 63) nl = 63;
    for (int i = 0; i < nl; i++) vname[i] = name[i];
    vname[nl] = 0;

    char *rest = (*p) ? p + 1 : p;
    rest = skip_ws(rest);

    char *words = 0;
    if (kw_at(rest, "in")) {
        words = skip_ws(rest + 2);
    }

    int dids[1] = { KW_DO };
    int did = -1;
    char *do_kw = find_kw0(words ? words : rest, dids, 1, &did);
    if (!do_kw) { fputs("shell: for: missing do\n", stdout); return 2; }
    *do_kw = 0;

    char *body = do_kw + 2;
    int d2ids[1] = { KW_DONE };
    int d2id = -1;
    char *done_kw = find_kw0(body, d2ids, 1, &d2id);
    if (!done_kw) { fputs("shell: for: missing done\n", stdout); return 2; }
    *done_kw = 0;

    /* words 必须在循环**开始前**一次性展开（POSIX）：body 里若 cd 或建了
     * 文件，glob 结果不该跟着变。而且要复制一份 —— 展开池是全局静态的，
     * body 执行一次就把它清空了。 */
    char wcopy[MAX_ARGC][LINE_SIZE];
    int wc = 0;
    if (words) {
        char *e = do_kw - 1;
        while (e >= words && (*e == ' ' || *e == '\t' || *e == '\n' || *e == ';')) *e-- = 0;
        char *wargv[MAX_ARGC];
        int wq[MAX_ARGC];
        int wa = parse_args(words, wargv, MAX_ARGC, wq);
        if (wa < 0) {
            printf("shell: for: too many words (max %d)\n", MAX_ARGC);
            return 2;
        }
        /* 变量展开要在 glob 之前，和 run_node 里的顺序一致 —— 少了这一步，
         * `for i in $LIST` 只会迭代一次，元素是字面量 "$LIST"。 */
        char *vargv[MAX_ARGC];
        int vq[MAX_ARGC];
        int va = expand_variables(wa, wargv, wq, vargv, vq);
        char *exp[MAX_ARGC];
        int ec = expand_args(va, vargv, exp, vq);
        for (int i = 0; i < ec && wc < MAX_ARGC; i++) {
            copy_str(wcopy[wc], exp[i], LINE_SIZE);
            wc++;
        }
    }

    int st = 0;
    g_loop_depth++;
    for (int i = 0; i < wc; i++) {
        setenv_s(vname, wcopy[i], 1);
        /* 每轮必须复制 body：run_node / run_pipeline 都是**原地**写 \0，
         * 不复制的话第二轮开始命令名就消失了（第一轮正常、第二轮空跑）。 */
        char bcopy[LINE_SIZE];
        copy_str(bcopy, body, LINE_SIZE);
        st = exec_list(bcopy);
        if (st == CTL_BREAK)    { st = 0; break; }
        if (st == CTL_CONTINUE) { st = 0; continue; }
    }
    g_loop_depth--;
    return st;
}

static int run_while(char *p, int until) {
    int dids[1] = { KW_DO };
    int did = -1;
    char *do_kw = find_kw0(p, dids, 1, &did);
    if (!do_kw) { fputs("shell: while: missing do\n", stdout); return 2; }
    *do_kw = 0;

    char *cond = p;
    char *body = do_kw + 2;
    int d2ids[1] = { KW_DONE };
    int d2id = -1;
    char *done_kw = find_kw0(body, d2ids, 1, &d2id);
    if (!done_kw) { fputs("shell: while: missing done\n", stdout); return 2; }
    *done_kw = 0;

    int st = 0;
    g_loop_depth++;
    for (;;) {
        char ccopy[LINE_SIZE];
        copy_str(ccopy, cond, LINE_SIZE);
        int c = exec_list(ccopy);
        int go = until ? (c != 0) : (c == 0);
        if (!go) break;

        char bcopy[LINE_SIZE];
        copy_str(bcopy, body, LINE_SIZE);
        st = exec_list(bcopy);
        if (st == CTL_BREAK)    { st = 0; break; }
        if (st == CTL_CONTINUE) { st = 0; continue; }
    }
    g_loop_depth--;
    return st;
}

static int run_command(char *cmd, int bg) {
    if (!*cmd) return 0;
    if (kw_at(cmd, "if"))    return run_if(cmd + 2);
    if (kw_at(cmd, "for"))   return run_for(cmd + 3);
    if (kw_at(cmd, "while")) return run_while(cmd + 5, 0);
    if (kw_at(cmd, "until")) return run_while(cmd + 5, 1);

    /* 落单的关键字当命令名交给 run_node 会变成 "if: not found"，
     * 这种报错会让人以为是 PATH 问题。 */
    {
        static const char *const stray[] = { "fi", "done", "then", "else", "elif", "do", "esac" };
        for (int i = 0; i < 7; i++) {
            if (kw_at(cmd, stray[i])) {
                fputs("shell: syntax error near unexpected `", stdout);
                fputs(stray[i], stdout);
                fputs("'\n", stdout);
                return 2;
            }
        }
    }
    return run_node(cmd, bg);
}

/* 对外的入口。break/continue 漏到最外层说明写错了（run_node 已报错），
 * 这里夹掉，免得 $? 变成 300 这种不可能的值。 */
static int execute_line(char *line) {
    int st = exec_list(line);
    if (st > CTL_MAX) st = 0;
    g_last_exit = st;
    return st;
}

static int builtin_source(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fputs("source: cannot open ", stdout);
        fputs(path, stdout);
        fputs("\n", stdout);
        return 1;
    }

    char buf[4096];
    int total = 0;
    while (total < (int)sizeof(buf) - 1) {
        isize n = read(fd, buf + total, sizeof(buf) - 1 - total);
        if (n <= 0) break;
        total += n;
    }
    buf[total] = 0;
    close(fd);

    int last_status = 0;
    int i = 0;

    /* 跨行累积：脚本里写成多行的 if/for 必须拼成一条语句才能执行，
     * 否则 `for i in 1 2` 那行会被单独执行掉（报 missing do）。 */
    char acc[LINE_SIZE];
    int alen = 0;
    acc[0] = 0;

    while (i < total) {
        /* 收集一个物理行，处理续行 \ */
        char line[LINE_SIZE];
        int len = 0;

        while (i < total && len < LINE_SIZE - 1) {
            char c = buf[i++];
            if (c == '\n') {
                if (len > 0 && line[len - 1] == '\\') {
                    len--;  /* 去掉反斜杠，继续读下一行 */
                    continue;
                }
                break;
            }
            if (c != '\r') {
                line[len++] = c;
            }
        }
        line[len] = 0;

        /* 空行和纯注释行：不影响深度，也不参与拼接 */
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == 0 || *p == '#') continue;

        /* 拼到 acc 上，中间补 ';' */
        if (alen > 0 && alen < LINE_SIZE - 2) acc[alen++] = ';';
        for (int t = 0; t < len && alen < LINE_SIZE - 1; t++) acc[alen++] = line[t];
        acc[alen] = 0;

        if (compound_depth(acc) > 0) continue;      /* 还没闭合，继续读 */

        last_status = execute_line(acc);
        alen = 0;
        acc[0] = 0;
    }
    /* 收尾时仍未闭合：执行它，让 shell 报 missing done/fi，
     * 总比静默丢掉一半脚本好。 */
    if (alen > 0) last_status = execute_line(acc);

    return last_status;
}



/* ---- main ---- */
int main(int argc, char *argv[]) {
    char line[LINE_SIZE];

    /* 程序搜索路径先加载: -c 分支和交互分支都要靠它查 PATH */
    load_search_dirs();

    /* `shell -c "cmd"`: POSIX 的非交互形态 —— 执行一条命令, 按它的退出码退出。*/
    if (argc >= 2 && strcmp(argv[1], "-c") == 0) {
        if (argc < 3) {
            fputs("shell: -c requires an argument\n", stderr);
            return 2;
        }
        return execute_line(argv[2]);
    }

    fputs("\nRmikuOS shell\n", stdout);
    print_help();

    signal(SIGINT, SIG_IGN);
    /* 提示符下按 Ctrl+Z 不该停住 shell：终端驱动是"把 SIGTSTP 发给前台"，
     * 而前台此时就是 shell 自己。子进程不受影响（fork 不复制处置表）。 */
    signal(SIGTSTP, SIG_IGN);

    /* shell 自己也要独立成组，并把前台交给自己：
     *   - 不然它与 init 同组，Ctrl+C 打向前台组时会连 init 一起打
     *   - 前台变量初值是 init 的组，提示符下按 Ctrl+C 会投错目标 */
    setpgid(0, 0);
    set_front(getpgrp());

    /* fake-hwclock 的定期快照。*/
    unsigned long last_stamp = 0;

    while (1) {
        time_t now = time(0);
        if (now > 0 &&
            (last_stamp == 0 || (unsigned long)now - last_stamp >= HWCLOCK_STAMP_INTERVAL)) {
            /* 失败也更新计数：/var 没挂载时不该每轮都去撞一次文件系统 */
            hwclock_stamp_write((unsigned long)now);
            last_stamp = (unsigned long)now;
        }

        reap_jobs();

        char cwd_buf[128];
        char prompt[128];
        int p = 0;
        if (getcwd(cwd_buf, sizeof(cwd_buf)) != NULL) {
            for (int i = 0; cwd_buf[i] && p < 126; i++) prompt[p++] = cwd_buf[i];
        }
        prompt[p++] = ' ';
        prompt[p++] = '$';
        prompt[p++] = ' ';
        prompt[p] = '\0';

        int len = read_line(prompt, line, LINE_SIZE);
        if (len == 0) continue;

        execute_line(line);
    }
    return 0;
}