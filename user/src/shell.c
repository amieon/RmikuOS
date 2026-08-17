#include "user.h"

#define LINE_SIZE 128
#define MAX_ARGC  16
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

struct job {
    int used;
    int id;
    isize pid;
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
        "shutdown", "ls", "cat", "clear", "jobs",
        "export", "env", "unset", NULL
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
static void add_job(isize pid, const char *cmd) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!jobs[i].used) {
            jobs[i].used = 1;
            jobs[i].id = next_job_id++;
            jobs[i].pid = pid;
            copy_str(jobs[i].cmd, cmd, LINE_SIZE);
            return;
        }
    }
}

static void reap_jobs(void) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].used) {
            int status;
            if (waitpid(jobs[i].pid, &status, WNOHANG) == jobs[i].pid) {
                printf("[%d] done %s\n", jobs[i].id, jobs[i].cmd);
                jobs[i].used = 0;
            }
        }
    }
}

static void print_jobs(void) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].used) {
            printf("[%d] running %s\n", jobs[i].id, jobs[i].cmd);
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
        if (argc >= max_argc) break;

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
                    if (line[i] == '\\') {
                        i++;
                        if (line[i]) {
                            line[w++] = line[i];
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
    fputs("  rename <old> <new> (mv <old> <new> also works; ...)\n",stdout);
    fputs("  jobs\n", stdout);
    fputs("  clear\n", stdout);
    fputs("  export NAME=VALUE   (set env var; $VAR / ${VAR} / $? expands in commands)\n", stdout);
    fputs("  env                 (print all env vars)\n", stdout);
    fputs("  unset NAME          (remove env var)\n", stdout);
    fputs("  variable expansion: $VAR, ${VAR}, $? (last exit code)\n", stdout);
    fputs("    'single quotes' protect $; \"double quotes\" still expand $ (bash style)\n", stdout);
    fputs("  shutdown\n", stdout);
    fputs("\nexternal commands are in $PATH (/bin, /tests, /programs):\n", stdout);
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



static void run_exec(int argc, char *argv[]){
    struct exec_args args;
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
        exec_with_args(argv[0], &args);
        fputs("exec failed: ", stdout);
        fputs(argv[0], stdout);
        fputs("\n", stdout);
        return;
    }

    exec_with_args(argv[0], &args);

    for (int d = 0; d < num_dirs; d++) {
        char path[96];
        build_exec_path(search_dirs[d], argv[0], path, sizeof(path));
        exec_with_args(path, &args);
    }

    fputs("command not found: ", stdout);
    fputs(argv[0], stdout);
    fputs("\n", stdout);
}

static int run_external(int argc, char *argv[], int background) {
    /* Refresh PATH each time before running an external command. */
    load_search_dirs();
    /* 外部命令(尤其 sqlite3/cat 这类交互程序)依赖内核回显: 打开它。
     * 本 shell 自带行编辑器, read_line() 时已关掉, 这里重新打开。 */
    set_echo(1);
    isize pid = fork();
    if (pid == 0) {
        /* Child stdin must stay BLOCKING. The old code set O_NONBLOCK on
        ** fd 0 before fork for Ctrl+C polling, and fd_flags get cloned
        ** into the child, so interactive programs (sqlite3 shell, cat,
        ** lua...) saw read()==0 (EOF) on stdin and exited immediately.
        ** Reset here; per-process fd_flags so this does not affect the
        ** parent. */
        fcntl(0, F_SETFL, 0);
        run_exec(argc, argv);
        exit(1);
    } else if (pid > 0) {
        if (background) {
            add_job(pid, argv[0]);
            printf("[%d] %d\n", next_job_id - 1, pid);
            set_echo(0);          /* 回 shell 提示符, 关回显 */
            set_front(getpid());  /* 后台任务不占前台: Ctrl+C 回 shell */
            return 0;
        }
        /* Foreground: block until the child exits. We no longer poll
        ** read(0) for Ctrl+C: that consumed input chars meant for the
        ** child from the shared terminal. Kernel signal delivery is
        ** incomplete; Ctrl+C interrupt can come back with a kernel
        ** Ctrl+C -> SIGINT implementation later. */
        int status = 0;
        while (waitpid(pid, &status, 0) < 0) yield();
        set_echo(0);   /* 回 shell 提示符, 关回显 */
        return WEXITSTATUS(status);
    } else {
        set_echo(0);
        fputs("fork failed\n", stdout);
        return 1;
    }
}

/* ---- 管道 & 重定向 ---- */
struct segment {
    char *cmd_str;
    char  infile[LINE_SIZE];
    char  outfile[LINE_SIZE];
    int   append;
};

static char *skip_spaces(char *p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

static int parse_redirect(char *seg_str, struct segment *out) {
    out->cmd_str   = seg_str;
    out->infile[0]  = 0;
    out->outfile[0] = 0;
    out->append     = 0;

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
            int append = 0;
            i++;
            if (is_output && seg_str[i] == '>') {
                append = 1;
                i++;
            }
            i = (int)(skip_spaces(seg_str + i) - seg_str);
            if (seg_str[i] == 0 ||
                seg_str[i] == '>' || seg_str[i] == '<' || seg_str[i] == '|') {
                return -1;
            }
            char target[LINE_SIZE];
            int t = 0;
            while (seg_str[i] && seg_str[i] != ' ' && seg_str[i] != '\t' &&
                   seg_str[i] != '>' && seg_str[i] != '<' && seg_str[i] != '|') {
                if (t < LINE_SIZE - 1) target[t++] = seg_str[i];
                i++;
            }
            target[t] = 0;
            if (is_output) {
                copy_str(out->outfile, target, LINE_SIZE);
                out->append = append;
            } else {
                copy_str(out->infile, target, LINE_SIZE);
            }
            continue;
        }
        seg_str[w++] = seg_str[i++];
    }
    seg_str[w] = 0;
    return 0;
}

static void apply_redirect_and_exec(struct segment *seg, int argc, char *argv[]) {
    if (seg->infile[0]) {
        isize fd = open(seg->infile, O_RDONLY);
        if (fd < 0) {
            printf("cannot open %s for input\n", seg->infile);
            exit(1);
        }
        dup2(fd, 0);
        close(fd);
    }
    if (seg->outfile[0]) {
        isize fd;
        if (seg->append)
            fd = open(seg->outfile, O_CREAT | O_APPEND | O_WRONLY, 0644);
        else
            fd = open(seg->outfile, O_CREAT | O_TRUNC | O_WRONLY, 0644);
        if (fd < 0) {
            printf("cannot open %s for output\n", seg->outfile);
            exit(1);
        }
        dup2(fd, 1);
        close(fd);
    }
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

    int last_status = 0;
    for (int i = 0; i < npid; i++) {
        int status;
        waitpid(pids[i], &status, 0);
        if (i == npid - 1) {
            last_status = WEXITSTATUS(status);
        }
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

/* ---- 单命令执行（含内置命令、通配展开、后台） ---- */
static int run_node(char *cmd, int background) {
    if (has_pipe_or_redirect(cmd)) {
        return run_pipeline(cmd);
    }

    int quoted[MAX_ARGC];
    char *argv[MAX_ARGC];
    int argc = parse_args(cmd, argv, MAX_ARGC, quoted);
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

    if (streq(exp_argv[0], "help")) { print_help(); return 0; }
    if (streq(exp_argv[0], "exit")) { fputs("bye\n", stdout); exit(0); }
    if (streq(exp_argv[0], "export")) { return builtin_export(exp_argc, exp_argv); }
    if (streq(exp_argv[0], "env")) { return builtin_env(exp_argc, exp_argv); }
    if (streq(exp_argv[0], "unset")) { return builtin_unset(exp_argc, exp_argv); }
    if (streq(exp_argv[0], "pwd")) { return builtin_pwd(); }
    if (streq(exp_argv[0], "cd")) { return builtin_cd(exp_argc, exp_argv); }
    if (streq(exp_argv[0], "mkdir")) { return builtin_mkdir(exp_argc, exp_argv); }
    if (streq(exp_argv[0], "touch")) { return builtin_create(exp_argc, exp_argv); }
    if (streq(exp_argv[0], "rm")) { return builtin_rm(exp_argc, exp_argv); }
    if (streq(exp_argv[0], "rmdir")) { return builtin_rmdir(exp_argc, exp_argv); }
    if (streq(exp_argv[0], "whoami")) { return builtin_whoami(); }
    if (streq(exp_argv[0], "chmod")) { return builtin_chmod(exp_argc, exp_argv); }
    if (streq(exp_argv[0], "chown")) { return builtin_chown(exp_argc, exp_argv); }
    if (streq(exp_argv[0], "rename") || streq(exp_argv[0], "mv")) { return builtin_rename(exp_argc, exp_argv); }
    if (streq(exp_argv[0], "jobs")) { print_jobs(); return 0; }
    if (streq(exp_argv[0], "clear")) { builtin_clear(); return 0; }
    if (streq(exp_argv[0], "source") || streq(exp_argv[0], ".")) {
        if (exp_argc < 2) {
            fputs("source: missing file operand\n", stdout);
            return 1;
        }
        return builtin_source(exp_argv[1]);
    }
    if (streq(exp_argv[0], "shutdown")) { fputs("bye bye~\n", stdout); shutdown(); return 0; }

    return run_external(exp_argc, exp_argv, background);
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
        while (waitpid(pid, &status, 0) < 0) yield();
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

/* 执行一行输入（处理 ; 和 && || 逻辑链） */
static int execute_line(char *line) {
    int last_status = 0;

    expand_cmdsub(line);   /* 命令替换必须在语句/逻辑分割之前完成 */

    /* 按 ; 分割语句 */
    char line_copy[LINE_SIZE];
    copy_str(line_copy, line, LINE_SIZE);

    char *stmts[16];
    int nstmt = 0;
    int quote = 0;
    char *start = line_copy;

    for (int i = 0; line_copy[i] && nstmt < 16; i++) {
        if (line_copy[i] == '\"' || line_copy[i] == '\'') {
            if (!quote) quote = line_copy[i];
            else if (quote == line_copy[i]) quote = 0;
        } else if (!quote && line_copy[i] == ';') {
            line_copy[i] = 0;
            while (*start == ' ' || *start == '\t') start++;
            if (*start) stmts[nstmt++] = start;
            start = line_copy + i + 1;
        }
    }
    while (*start == ' ' || *start == '\t') start++;
    if (*start && nstmt < 16) stmts[nstmt++] = start;

    /* 逐条语句执行 */
    for (int s = 0; s < nstmt; s++) {
        /* 按 && / || 分割逻辑节点 */
        struct logic_node { char *cmd; int op; int bg; };
        struct logic_node nodes[16];
        int nnode = 0;

        char *p = stmts[s];
        char *seg_start = p;
        quote = 0;

        for (int i = 0; p[i] && nnode < 16; i++) {
            if (p[i] == '\"' || p[i] == '\'') {
                if (!quote) quote = p[i];
                else if (quote == p[i]) quote = 0;
                continue;
            }
            if (quote) continue;

            if (p[i] == '&' && p[i+1] == '&') {
                p[i] = 0;
                int bg = 0;
                char *end = p + i - 1;
                while (end > seg_start && (*end == ' ' || *end == '\t')) {
                    *end = 0; end--;
                }
                if (end >= seg_start && *end == '&') {
                    bg = 1; *end = 0; end--;
                    while (end > seg_start && (*end == ' ' || *end == '\t')) {
                        *end = 0; end--;
                    }
                }
                while (*seg_start == ' ' || *seg_start == '\t') seg_start++;
                nodes[nnode].cmd = seg_start;
                nodes[nnode].op = 1; /* AND */
                nodes[nnode].bg = bg;
                nnode++; i++; seg_start = p + i + 1;
            } else if (p[i] == '|' && p[i+1] == '|') {
                p[i] = 0;
                int bg = 0;
                char *end = p + i - 1;
                while (end > seg_start && (*end == ' ' || *end == '\t')) {
                    *end = 0; end--;
                }
                if (end >= seg_start && *end == '&') {
                    bg = 1; *end = 0; end--;
                    while (end > seg_start && (*end == ' ' || *end == '\t')) {
                        *end = 0; end--;
                    }
                }
                while (*seg_start == ' ' || *seg_start == '\t') seg_start++;
                nodes[nnode].cmd = seg_start;
                nodes[nnode].op = 2; /* OR */
                nodes[nnode].bg = bg;
                nnode++; i++; seg_start = p + i + 1;
            }
        }

        if (nnode < 16 && *seg_start) {
            int bg = 0;
            char *end = seg_start + strlen_(seg_start) - 1;
            while (end > seg_start && (*end == ' ' || *end == '\t')) {
                *end = 0; end--;
            }
            if (end >= seg_start && *end == '&') {
                bg = 1; *end = 0; end--;
                while (end > seg_start && (*end == ' ' || *end == '\t')) {
                    *end = 0; end--;
                }
            }
            while (*seg_start == ' ' || *seg_start == '\t') seg_start++;
            nodes[nnode].cmd = seg_start;
            nodes[nnode].op = 0;
            nodes[nnode].bg = bg;
            nnode++;
        }

        for (int i = 0; i < nnode; i++) {
            if (i > 0) {
                if (nodes[i-1].op == 1 && last_status != 0) continue;
                if (nodes[i-1].op == 2 && last_status == 0) continue;
            }
            last_status = run_node(nodes[i].cmd, nodes[i].bg);
            g_last_exit = last_status;   /* 供 $? 展开使用 */
        }
    }

    return last_status;
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

    while (i < total) {
        /* 收集一行，处理续行 \ */
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

        /* 跳过空行和注释 */
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == 0 || *p == '#') continue;

        last_status = execute_line(line);
    }

    return last_status;
}



/* ---- main ---- */
int main(void) {
    char line[LINE_SIZE];

    fputs("\nRmikuOS shell\n", stdout);
    print_help();
    load_search_dirs();

    signal(SIGINT, SIG_IGN);

    while (1) {
        set_my_tickets(1);
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