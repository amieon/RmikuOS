/* 宿主侧验证复合命令的**执行**行为。
 * 扫描器与执行层都 include 自 shell.c 的真实代码，只有底层命令是桩。 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define LINE_SIZE 512
#define MAX_ARGC  16

#include "_scan.inc"

/* ---- 桩：底层依赖 ---- */
static int g_last_exit = 0;
static char logbuf[8192];

static void logadd(const char *s) {
    strncat(logbuf, s, sizeof logbuf - strlen(logbuf) - 2);
    strncat(logbuf, "\n", 2);
}

static void copy_str(char *d, const char *s, int n) {
    int i = 0;
    while (s[i] && i < n - 1) { d[i] = s[i]; i++; }
    d[i] = 0;
}

static char envtab[32][2][64];
static int envc = 0;
static void setenv_s(const char *k, const char *v, int ow) {
    (void)ow;
    for (int i = 0; i < envc; i++)
        if (!strcmp(envtab[i][0], k)) { strncpy(envtab[i][1], v, 63); return; }
    strncpy(envtab[envc][0], k, 63);
    strncpy(envtab[envc][1], v, 63);
    envc++;
}
static const char *env_get(const char *k) {
    for (int i = 0; i < envc; i++)
        if (!strcmp(envtab[i][0], k)) return envtab[i][1];
    return 0;
}

static void expand_cmdsub(char *line) { (void)line; }

static int parse_args(char *line, char *argv[], int max_argc, int quoted[]) {
    int argc = 0;
    char *p = line;
    while (*p && argc < max_argc) {
        while (*p == ' ' || *p == '\t' || *p == '\n') p++;
        if (!*p) break;
        int q = 0;
        if (*p == '\'' || *p == '"') { q = (*p == '\'') ? 1 : 2; p++; }
        argv[argc] = p;
        if (q) {
            char close = (q == 1) ? '\'' : '"';
            while (*p && *p != close) p++;
            if (*p) { *p = 0; p++; }
        } else {
            while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
            if (*p) *p++ = 0;
        }
        quoted[argc] = q;
        argc++;
    }
    return argc;
}

static int expand_variables(int argc, char *argv[], int quoted[],
                            char *outv[], int out_quoted[]) {
    for (int i = 0; i < argc; i++) {
        if (quoted[i] == 1) { outv[i] = argv[i]; out_quoted[i] = 1; continue; }
        static char pool[16][256];
        char *buf = pool[i & 15];
        int o = 0;
        for (int j = 0; argv[i][j] && o < 250; j++) {
            if (argv[i][j] == '$' && argv[i][j+1]) {
                if (argv[i][j+1] == '?') { o += sprintf(buf + o, "%d", g_last_exit); j++; continue; }
                int k = 0;
                char nm[64];
                j++;
                while (argv[i][j] && (isalnum((unsigned char)argv[i][j]) || argv[i][j] == '_') && k < 63)
                    nm[k++] = argv[i][j++];
                nm[k] = 0;
                j--;
                const char *v = env_get(nm);
                if (v) while (*v && o < 250) buf[o++] = *v++;
                continue;
            }
            buf[o++] = argv[i][j];
        }
        buf[o] = 0;
        outv[i] = buf;
        out_quoted[i] = quoted[i];
    }
    return argc;
}

static int expand_args(int argc, char *argv[], char *outv[], int quoted[]) {
    for (int i = 0; i < argc; i++) outv[i] = argv[i];
    return argc;
}

static int run_node(char *cmd, int bg) {
    (void)bg;
    char *argv[16];
    int q[16];
    int argc = parse_args(cmd, argv, 16, q);
    if (argc == 0) return 0;

    char *v[16];
    int vq[16];
    int vc = expand_variables(argc, argv, q, v, vq);

    /* break/continue 在真实 run_node 里也是同进程内置，桩要对齐 */
    if (!strcmp(v[0], "break") || !strcmp(v[0], "continue")) {
        if (g_loop_depth <= 0) return 0;
        return !strcmp(v[0], "break") ? CTL_BREAK : CTL_CONTINUE;
    }
    if (!strcmp(v[0], "ok")) return 0;
    if (!strcmp(v[0], "bad")) return 1;
    if (!strcmp(v[0], "echo")) {
        char line[512];
        int o = 0;
        for (int i = 1; i < vc; i++) {
            if (i > 1) line[o++] = ' ';
            for (int t = 0; v[i][t] && o < 500; t++) line[o++] = v[i][t];
        }
        line[o] = 0;
        logadd(line);
        return 0;
    }
    char m[128];
    snprintf(m, sizeof m, "(?%s)", v[0]);
    logadd(m);
    return 127;
}

#include "_exec.inc"

/* ---- 测试 ---- */
static int fails = 0, total = 0;

static void run(const char *script, const char *want, int want_rc) {
    char buf[512];
    copy_str(buf, script, sizeof buf);
    logbuf[0] = 0;
    int rc = execute_line(buf);
    total++;
    if (strcmp(logbuf, want) != 0 || rc != want_rc) {
        fails++;
        printf("FAIL  %s\n        got  [%s] rc=%d\n        want [%s] rc=%d\n",
               script, logbuf, rc, want, want_rc);
    } else {
        printf("ok    %s\n", script);
    }
}

int main(void) {
    run("for i in a b c; do echo $i; done", "a\nb\nc\n", 0);
    run("for i in 1 2 3 4 5; do echo x$i; done", "x1\nx2\nx3\nx4\nx5\n", 0);
    run("for i in a b c; do echo $i; break; done", "a\n", 0);
    run("for i in a b c; do bad; continue; echo $i; done", "", 0);
    run("for i in 1 2; do for j in x y; do echo $i$j; done; done",
        "1x\n1y\n2x\n2y\n", 0);

    run("if ok; then echo yes; fi", "yes\n", 0);
    run("if bad; then echo yes; else echo no; fi", "no\n", 0);
    run("if bad; then echo a; elif ok; then echo b; else echo c; fi", "b\n", 0);
    run("if bad; then echo a; elif bad; then echo b; else echo c; fi", "c\n", 0);
    run("if bad; then echo a; elif bad; then echo b; fi", "", 0);
    run("if ok; then echo a; fi; echo after", "a\nafter\n", 0);
    run("if ok; then for i in 1 2; do echo $i; done; fi", "1\n2\n", 0);
    run("for i in a b; do if ok; then echo $i; fi; done", "a\nb\n", 0);
    run("if ok; then echo a; fi && echo b", "a\nb\n", 0);
    run("if bad; then echo a; fi || echo b", "", 0);

    run("while ok; do echo x; break; done", "x\n", 0);
    run("while bad; do echo x; done", "", 0);
    run("until ok; do echo x; done", "", 0);
    run("until bad; do echo x; break; done", "x\n", 0);

    run("bad; echo $?", "1\n", 0);
    run("if bad; then echo a; fi; echo $?", "0\n", 0);
    run("for i in a; do bad; done; echo $?", "1\n", 0);

    printf("\n%d/%d passed\n", total - fails, total);
    return fails ? 1 : 0;
}
