/* 宿主侧验证 shell 的复合命令"词法层"。
 * 直接 include 从 shell.c 抽出来的真实代码（_scan.inc），不复制实现。 */
#include <stdio.h>
#include <string.h>

#include "_scan.inc"

static int fails = 0;
static int total = 0;

static void ck(int ok, const char *what, const char *in, int got, int want) {
    total++;
    if (ok) {
        printf("ok    %-30s [%s] -> %d\n", what, in, got);
    } else {
        fails++;
        printf("FAIL  %-30s [%s] -> %d (want %d)\n", what, in, got, want);
    }
}

static int count_sep(const char *s, int kind) {
    char buf[512];
    struct sep seps[32];
    strncpy(buf, s, sizeof buf - 1);
    buf[sizeof buf - 1] = 0;
    int n = scan_seps(buf, seps, 32);
    int c = 0;
    for (int i = 0; i < n; i++)
        if (kind == 0 || seps[i].kind == kind) c++;
    return c;
}

int main(void) {
    /* --- scan_seps：复合命令体内部的分号不能切 --- */
    ck(count_sep("if a; then b; fi", 0) == 0, "if 体内不切", "if a; then b; fi", count_sep("if a; then b; fi", 0), 0);
    ck(count_sep("for i in 1 2 3; do echo $i; done", 0) == 0, "for 体内不切", "for...done", count_sep("for i in 1 2 3; do echo $i; done", 0), 0);
    ck(count_sep("a; b; c", SEP_SEMI) == 2, "普通分号", "a; b; c", count_sep("a; b; c", SEP_SEMI), 2);
    ck(count_sep("a && b || c", SEP_AND) == 1 && count_sep("a && b || c", SEP_OR) == 1,
       "&& / ||", "a && b || c", count_sep("a && b || c", SEP_AND), 1);
    ck(count_sep("echo 'a; b'", 0) == 0, "单引号保护", "echo 'a; b'", count_sep("echo 'a; b'", 0), 0);
    ck(count_sep("echo \"x && y\"", 0) == 0, "双引号保护", "echo \"x && y\"", count_sep("echo \"x && y\"", 0), 0);
    ck(count_sep("if a; then for i in x; do y; done; fi", 0) == 0, "嵌套不切", "if{for{}}", count_sep("if a; then for i in x; do y; done; fi", 0), 0);
    ck(count_sep("if a; then b; fi && echo ok", SEP_AND) == 1, "fi 之后的 &&", "if..fi && echo", count_sep("if a; then b; fi && echo ok", SEP_AND), 1);
    ck(count_sep("while true; do break; done; echo after", SEP_SEMI) == 1, "done 之后的分号", "while..done; echo", count_sep("while true; do break; done; echo after", SEP_SEMI), 1);
    ck(count_sep("echo if", 0) == 0, "echo if 不算关键字", "echo if", count_sep("echo if", 0), 0);

    /* --- compound_depth：多行续读的判据 --- */
    ck(compound_depth("if a; then") == 1, "if 未闭合", "if a; then", compound_depth("if a; then"), 1);
    ck(compound_depth("if a; then b; fi") == 0, "if 已闭合", "if..fi", compound_depth("if a; then b; fi"), 0);
    ck(compound_depth("for i in 1 2") == 1, "for 未闭合", "for i in 1 2", compound_depth("for i in 1 2"), 1);
    ck(compound_depth("for i in 1 2; do echo; done") == 0, "for 已闭合", "for..done", compound_depth("for i in 1 2; do echo; done"), 0);
    ck(compound_depth("if a; then for i in x; do y; done") == 1, "内层闭合外层未", "if{for{}}", compound_depth("if a; then for i in x; do y; done"), 1);
    ck(compound_depth("echo if then") == 0, "参数里的关键字", "echo if then", compound_depth("echo if then"), 0);
    ck(compound_depth("echo 'if x'") == 0, "引号里的关键字", "echo 'if x'", compound_depth("echo 'if x'"), 0);

    /* --- find_kw0：嵌套时 fi 归属 --- */
    {
        char buf[256];
        int ids[3] = { KW_ELIF, KW_ELSE, KW_FI };
        int id = -1;
        strcpy(buf, "if b; then c; fi; fi");
        char *p = find_kw0(buf, ids, 3, &id);
        ck(p && strcmp(p, "fi") == 0 && p == buf + 18,
           "嵌套找外层 fi", "if b; then c; fi; fi", p ? (int)(p - buf) : -1, 18);

        int dids[1] = { KW_DO };
        int did = -1;
        strcpy(buf, "1 2 3; do echo $i; done");
        p = find_kw0(buf, dids, 1, &did);
        ck(p && p == buf + 7, "for 的 words 后找 do", "1 2 3; do ...", p ? (int)(p - buf) : -1, 7);

        int tids[1] = { KW_THEN };
        int tid = -1;
        strcpy(buf, "a && b; then echo x; fi");
        p = find_kw0(buf, tids, 1, &tid);
        ck(p && p == buf + 8, "条件里含 && 找 then", "a && b; then", p ? (int)(p - buf) : -1, 7);
    }

    /* --- strip_comment --- */
    {
        char buf[128];
        strcpy(buf, "echo a # trailing");
        strip_comment(buf);
        ck(strcmp(buf, "echo a ") == 0, "裁行尾注释", "echo a # ...", (int)strlen(buf), 7);

        strcpy(buf, "echo 'a # b'");
        strip_comment(buf);
        ck(strcmp(buf, "echo 'a # b'") == 0, "引号内 # 保留", "echo 'a # b'", (int)strlen(buf), 12);

        strcpy(buf, "echo a#b");
        strip_comment(buf);
        ck(strcmp(buf, "echo a#b") == 0, "非词首 # 保留", "echo a#b", (int)strlen(buf), 8);
    }

    printf("\n%d/%d passed\n", total - fails, total);
    return fails ? 1 : 0;
}
