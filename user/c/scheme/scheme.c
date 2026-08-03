/* scheme.c —— 微型 Scheme 解释器（RmikuOS 第六门用户态语言，从零手写）
 *
 * 支持（教学核心集 + 尾调用优化 TCO）:
 *   数据类型: 整数 / 符号(intern) / 字符串 / 布尔(#t #f) / 空表() / 序对 / lambda / 内建
 *   特殊形式: quote  if  define  lambda  begin  set!  cond  let  and  or
 *   内建函数: + - * /  = < > <= >=  car cdr cons  null? pair? number? symbol?
 *             string? boolean?  not eq? list  display newline  exit
 *   TCO: 尾位置的 lambda 调用不进递归, 主循环迭代 -> 深尾递归不爆栈
 *   语法糖: 'x = (quote x); ; 行注释; 多行表达式(REPL 自动补全括号)
 *
 * 教学取舍(与 README 一致): 无 GC(REPL 长跑会泄漏, 注释说明)、无浮点、
 * 无变长参数、无 call/cc。文件模式: scheme file.scm
 *
 * 用法:
 *   scheme                 交互 REPL
 *   scheme /codes/xxx.scm  执行脚本文件
 */
#include "user.h"
#include "stdlib.h"

/* ================= 对象 ================= */
typedef enum {
    T_NUM, T_SYM, T_STR, T_BOOL, T_NIL, T_PAIR, T_LAMBDA, T_BUILTIN
} ObjType;

typedef struct Env Env;

typedef struct Obj Obj;
struct Obj {
    ObjType type;
    int gc;                  /* GC 标记(可达性) */
    union {
        long num;                    /* T_NUM */
        char *sym;                   /* T_SYM: intern 符号, 指针即身份 */
        char *str;                   /* T_STR */
        struct { Obj *car, *cdr; } pair;   /* T_PAIR / T_NIL(cdr 链尾) */
        struct {                    /* T_LAMBDA: 闭包 */
            char **params;
            int nparams;
            Obj *body;
            Env *env;
        } lambda;
        Obj *(*builtin)(Obj *args);  /* T_BUILTIN: 收参数列表 */
    } u;
};

/* ================= 符号 intern ================= */
static char **symtab = NULL;
static int nsym = 0, capsym = 0;

static char *intern(char *s) {
    for (int i = 0; i < nsym; i++)
        if (strcmp(symtab[i], s) == 0)
            return symtab[i];
    if (nsym >= capsym) {
        capsym = capsym ? capsym * 2 : 64;
        char **nt = (char **)malloc(sizeof(char *) * capsym);
        for (int i = 0; i < nsym; i++) nt[i] = symtab[i];
        free(symtab);
        symtab = nt;
    }
    char *copy = (char *)malloc(strlen(s) + 1);
    strcpy(copy, s);
    symtab[nsym++] = copy;
    return copy;
}

/* ---- 特殊形式预 intern 符号: C 的字符串字面量指针 != intern 表指针,
 * 必须预 intern 后按指针比较(与 Python 验证器的语言级 intern 语义对齐) ---- */
static char *S_quote, *S_if, *S_define, *S_lambda, *S_begin,
            *S_set, *S_cond, *S_else, *S_let, *S_and, *S_or;

static void init_special_symbols(void) {
    S_quote  = intern("quote");
    S_if     = intern("if");
    S_define = intern("define");
    S_lambda = intern("lambda");
    S_begin  = intern("begin");
    S_set    = intern("set!");
    S_cond   = intern("cond");
    S_else   = intern("else");
    S_let    = intern("let");
    S_and    = intern("and");
    S_or     = intern("or");
}

/* ================= 构造 ================= */
/* ---- GC: 标记-清除(mark-sweep) ----
 * 背景: 无 GC 时 TCO 循环每轮泄漏 Env+cons, 100 万次 = 300MB, 6MB 用户堆必爆。
 * 根集: 全局环境 + eval 主循环活跃(e/env) + REPL 当前表达式(expr)。
 * 触发: 每 GC_THRESHOLD 次对象分配, 在 eval 主循环顶部检查(递归子调用不触发,
 *       避免扫描到求值中未完成的中间值)。 */
static Obj **g_objs = NULL; static int n_objs = 0, cap_objs = 0;
static Env **g_envs = NULL; static int n_envs = 0, cap_envs = 0;
static int alloc_count = 0;
#define GC_THRESHOLD 20000

static Obj *gc_root_expr = NULL;   /* REPL/run_file 当前表达式 */
static Obj *gc_root_e = NULL;      /* eval 主循环当前表达式 */
static Env *gc_root_env = NULL;    /* eval 主循环当前环境 */
static Env *g_global = NULL;       /* 全局环境(永远可达) */

static void gc_mark_obj(Obj *o);
static void gc_mark_env(Env *e);
static void gc_sweep_envs(void);   /* 在 struct Env 定义后实现 */

static void gc_collect(void) {
    /* mark */
    gc_mark_env(g_global);
    gc_mark_obj(gc_root_expr);
    gc_mark_obj(gc_root_e);
    gc_mark_env(gc_root_env);
    /* sweep objs */
    int w = 0;
    for (int i = 0; i < n_objs; i++) {
        Obj *o = g_objs[i];
        if (o->gc) { o->gc = 0; g_objs[w++] = o; }
        else {
            if (o->type == T_STR) free(o->u.str);
            if (o->type == T_LAMBDA) free(o->u.lambda.params);
            free(o);
        }
    }
    n_objs = w;
    /* sweep envs */
    gc_sweep_envs();
    alloc_count = 0;
}

static void gc_register_obj(Obj *o) {
    if (n_objs >= cap_objs) {
        cap_objs = cap_objs ? cap_objs * 2 : 1024;
        Obj **nt = (Obj **)realloc(g_objs, sizeof(Obj *) * cap_objs);
        if (!nt) return;
        g_objs = nt;
    }
    g_objs[n_objs++] = o;
    alloc_count++;
}

static void gc_register_env(Env *e) {
    if (n_envs >= cap_envs) {
        cap_envs = cap_envs ? cap_envs * 2 : 256;
        Env **nt = (Env **)realloc(g_envs, sizeof(Env *) * cap_envs);
        if (!nt) return;
        g_envs = nt;
    }
    g_envs[n_envs++] = e;
    alloc_count++;
}

static Obj *obj_new(ObjType t) {
    Obj *o = (Obj *)malloc(sizeof(Obj));
    if (!o) return NULL;
    o->type = t;
    o->gc = 0;
    gc_register_obj(o);
    return o;
}
static Obj *num(long v)      { Obj *o = obj_new(T_NUM);  o->u.num = v; return o; }
static Obj *sym(char *s)     { Obj *o = obj_new(T_SYM);  o->u.sym = intern(s); return o; }
static Obj *str_obj(char *s) { Obj *o = obj_new(T_STR);  o->u.str = s; return o; }
static Obj *boolean(int b)   { Obj *o = obj_new(T_BOOL); o->u.num = b; return o; }
static Obj *nil_obj(void)    { Obj *o = obj_new(T_NIL); return o; }
static Obj *cons(Obj *car, Obj *cdr) {
    Obj *o = obj_new(T_PAIR);
    o->u.pair.car = car;
    o->u.pair.cdr = cdr;
    return o;
}

static const Obj *NIL = NULL;   /* 惰性: nil_obj() 每次新建, 无全局单例 */

/* ================= 环境 ================= */
struct Env {
    Env *parent;
    char **names;
    Obj **vals;
    int n, cap;
    int gc;                  /* GC 标记 */
};

static void gc_sweep_envs(void) {
    int w = 0;
    for (int i = 0; i < n_envs; i++) {
        Env *e = g_envs[i];
        if (e->gc) { e->gc = 0; g_envs[w++] = e; }
        else { free(e->names); free(e->vals); free(e); }
    }
    n_envs = w;
}

static Env *env_new(Env *parent) {
    Env *e = (Env *)malloc(sizeof(Env));
    if (!e) return NULL;
    e->parent = parent;
    e->names = NULL;
    e->vals = NULL;
    e->n = e->cap = 0;
    e->gc = 0;
    gc_register_env(e);
    return e;
}

static void gc_mark_obj(Obj *o) {
    if (!o || o->gc) return;
    o->gc = 1;
    if (o->type == T_PAIR) {
        gc_mark_obj(o->u.pair.car);
        gc_mark_obj(o->u.pair.cdr);
    } else if (o->type == T_LAMBDA) {
        gc_mark_obj(o->u.lambda.body);
        gc_mark_env(o->u.lambda.env);
    }
}

static void gc_mark_env(Env *e) {
    if (!e || e->gc) return;
    e->gc = 1;
    gc_mark_env(e->parent);
    for (int i = 0; i < e->n; i++) gc_mark_obj(e->vals[i]);
}

static void env_def(Env *e, char *name, Obj *val) {
    /* 已存在则覆盖(重定义) */
    for (int i = 0; i < e->n; i++)
        if (strcmp(e->names[i], name) == 0) {
            e->vals[i] = val;
            return;
        }
    if (e->n >= e->cap) {
        e->cap = e->cap ? e->cap * 2 : 16;
        char **nn = (char **)malloc(sizeof(char *) * e->cap);
        Obj **nv = (Obj **)malloc(sizeof(Obj *) * e->cap);
        for (int i = 0; i < e->n; i++) { nn[i] = e->names[i]; nv[i] = e->vals[i]; }
        free(e->names); free(e->vals);
        e->names = nn; e->vals = nv;
    }
    e->names[e->n] = name;
    e->vals[e->n] = val;
    e->n++;
}

/* set!: 沿父链查找, 找到则改值; 找不到返回 0 */
static int env_set(Env *e, char *name, Obj *val) {
    for (Env *p = e; p; p = p->parent)
        for (int i = 0; i < p->n; i++)
            if (strcmp(p->names[i], name) == 0) {
                p->vals[i] = val;
                return 1;
            }
    return 0;
}

static Obj *env_get(Env *e, char *name) {
    for (Env *p = e; p; p = p->parent)
        for (int i = 0; i < p->n; i++)
            if (strcmp(p->names[i], name) == 0)
                return p->vals[i];
    return NULL;
}

/* ================= 读取器 ================= */
static int src_pos = 0;

static void skip_ws(char *src) {
    while (src[src_pos]) {
        char c = src[src_pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { src_pos++; }
        else if (c == ';') {                    /* 行注释 */
            while (src[src_pos] && src[src_pos] != '\n') src_pos++;
        } else break;
    }
}

static Obj *read_expr(char *src);   /* fwd */

static Obj *read_list(char *src) {
    skip_ws(src);
    if (src[src_pos] == ')') { src_pos++; return nil_obj(); }
    Obj *car = read_expr(src);
    if (!car) return NULL;
    Obj *cdr = read_list(src);
    if (!cdr) return NULL;
    return cons(car, cdr);
}

static Obj *read_expr(char *src) {
    skip_ws(src);
    char c = src[src_pos];
    if (c == '\0') return NULL;

    if (c == '(') {
        src_pos++;
        return read_list(src);
    }
    if (c == ')') {                 /* 多余右括号: 错误 */
        return NULL;
    }
    if (c == '\'') {                /* 'x = (quote x) */
        src_pos++;
        Obj *x = read_expr(src);
        if (!x) return NULL;
        return cons(sym("quote"), cons(x, nil_obj()));
    }
    if (c == '"') {                 /* 字符串 */
        src_pos++;
        char buf[1024];
        int n = 0;
        while (src[src_pos] && src[src_pos] != '"') {
            if (src[src_pos] == '\\' && src[src_pos + 1] == 'n') {
                buf[n++] = '\n'; src_pos += 2;
            } else {
                buf[n++] = src[src_pos++];
            }
        }
        if (!src[src_pos]) return NULL;
        src_pos++;                  /* 吃掉闭合 " */
        buf[n] = '\0';
        char *copy = (char *)malloc(n + 1);
        strcpy(copy, buf);
        return str_obj(copy);
    }

    /* 原子: 读到一个分隔符为止 */
    char tok[256];
    int n = 0;
    while (src[src_pos] && !strchr(" \t\n\r();'\"", src[src_pos])) {
        if (n < 255) tok[n++] = src[src_pos];
        src_pos++;
    }
    tok[n] = '\0';
    if (n == 0) return NULL;

    /* #t / #f */
    if (strcmp(tok, "#t") == 0) return boolean(1);
    if (strcmp(tok, "#f") == 0) return boolean(0);

    /* 数字? */
    int is_num = 1;
    if (n == 0 || (tok[0] < '0' || tok[0] > '9') && tok[0] != '-' && tok[0] != '+')
        is_num = 0;
    if (is_num) {
        for (int i = 1; i < n; i++)
            if (tok[i] < '0' || tok[i] > '9') { is_num = 0; break; }
        /* 单独 +/- 不是数字 */
        if (n == 1 && (tok[0] == '-' || tok[0] == '+')) is_num = 0;
    }
    if (is_num) {
        long v = 0, sign = 1;
        int i = 0;
        if (tok[0] == '-') { sign = -1; i = 1; }
        else if (tok[0] == '+') i = 1;
        for (; i < n; i++) v = v * 10 + (tok[i] - '0');
        return num(sign * v);
    }
    return sym(tok);
}

/* 括号平衡检查(忽略字符串/注释)—— REPL 跨行补全用 */
static int balanced(char *s) {
    int depth = 0, in_str = 0;
    for (int i = 0; s[i]; i++) {
        char c = s[i];
        if (in_str) {
            if (c == '"' && (i == 0 || s[i - 1] != '\\')) in_str = 0;
            continue;
        }
        if (c == '"') { in_str = 1; continue; }
        if (c == ';') { while (s[i] && s[i] != '\n') i++; continue; }
        if (c == '(') depth++;
        else if (c == ')') depth--;
        if (depth < 0) depth = 0;   /* 容错 */
    }
    return depth <= 0;
}

/* ================= 求值器 (TCO 主循环) ================= */
static Obj *eval(Obj *e, Env *env, int tail);


/* body 是 (expr...) 列表 -> 单表达式直接返回, 多表达式包成 (begin ...) */
static Obj *body_of(Obj *body_list) {
    if (body_list->type == T_NIL) return nil_obj();
    if (body_list->u.pair.cdr->type == T_NIL) return body_list->u.pair.car;
    return cons(sym("begin"), body_list);
}

static Obj *eval_list(Obj *args, Env *env) {
    if (args->type == T_NIL) return nil_obj();
    Obj *car = eval(args->u.pair.car, env, 0);
    if (!car) return NULL;
    Obj *cdr = eval_list(args->u.pair.cdr, env);
    if (!cdr) return NULL;
    return cons(car, cdr);
}

static Obj *eval(Obj *e, Env *env, int tail) {
    for (;;) {
        if (alloc_count > GC_THRESHOLD) {
            gc_root_e = e;
            gc_root_env = env;
            gc_collect();
        }
        if (!e) return NULL;
        switch (e->type) {
        case T_NUM: case T_STR: case T_BOOL: case T_NIL:
            return e;
        case T_SYM: {
            Obj *v = env_get(env, e->u.sym);
            if (!v) {
                printf("scheme: unbound symbol: %s\n", e->u.sym);
                return NULL;
            }
            return v;
        }
        case T_LAMBDA: case T_BUILTIN:
            return e;
        case T_PAIR: break;
        }

        /* ---- e 是列表 (head rest...) ---- */
        Obj *head = e->u.pair.car;
        if (head->type != T_SYM) {
            /* 非符号头部: ( (lambda ...) args... ) 直接调用 */
            Obj *fn = eval(head, env, 0);
            if (!fn) return NULL;
            Obj *args = eval_list(e->u.pair.cdr, env);
            if (!args) return NULL;
            if (fn->type == T_BUILTIN)
                return fn->u.builtin(args);
            if (fn->type == T_LAMBDA) {
                Env *n = env_new(fn->u.lambda.env);
                Obj *a = args;
                for (int i = 0; i < fn->u.lambda.nparams; i++) {
                    if (a->type == T_NIL) break;
                    env_def(n, fn->u.lambda.params[i], a->u.pair.car);
                    a = a->u.pair.cdr;
                }
                if (tail) { e = fn->u.lambda.body; env = n; continue; }  /* TCO */
                return eval(fn->u.lambda.body, n, 0);
            }
            printf("scheme: not a function\n");
            return NULL;
        }

        char *h = head->u.sym;

        /* ---- 特殊形式 ---- */
        if (h == S_quote) {
            return e->u.pair.cdr->u.pair.car;   /* (quote x) */
        }
        if (h == S_if) {
            Obj *cond = eval(e->u.pair.cdr->u.pair.car, env, 0);
            if (!cond) return NULL;
            int truth = !(cond->type == T_BOOL && cond->u.num == 0);
            Obj *rest = e->u.pair.cdr->u.pair.cdr;   /* (then else) */
            Obj *branch = truth ? rest->u.pair.car
                                : (rest->u.pair.cdr->type == T_NIL ? NULL
                                   : rest->u.pair.cdr->u.pair.car);
            if (!branch) return boolean(0);          /* 无 else 且为假 */
            e = branch;
            tail = 1;                                /* TCO: 尾位置(传播标志) */
            continue;
        }
        if (h == S_begin) {
            Obj *body = e->u.pair.cdr;
            while (body->type == T_PAIR && body->u.pair.cdr->type == T_PAIR) {
                Obj *r = eval(body->u.pair.car, env, 0);
                if (!r) return NULL;
                body = body->u.pair.cdr;
            }
            if (body->type == T_NIL) return nil_obj();
            e = body->u.pair.car;                    /* 最后一项: 尾位置 */
            tail = 1;
            continue;
        }
        if (h == S_define) {
            Obj *target = e->u.pair.cdr->u.pair.car;   /* 符号 或 (f params...) */
            Obj *rest = e->u.pair.cdr->u.pair.cdr;
            if (target->type == T_SYM) {
                Obj *val = eval(rest->u.pair.car, env, 0);
                if (!val) return NULL;
                env_def(env, target->u.sym, val);
                return nil_obj();
            }
            /* 函数定义糖: (define (f a b) body) */
            if (target->type == T_PAIR && target->u.pair.car->type == T_SYM) {
                Obj *params = target->u.pair.cdr;
                int n = 0;
                for (Obj *p = params; p->type == T_PAIR; p = p->u.pair.cdr) n++;
                char **names = (char **)malloc(sizeof(char *) * n);
                int i = 0;
                for (Obj *p = params; p->type == T_PAIR; p = p->u.pair.cdr)
                    names[i++] = p->u.pair.car->u.sym;
                Obj *fn = obj_new(T_LAMBDA);
                fn->u.lambda.params = names;
                fn->u.lambda.nparams = n;
                fn->u.lambda.body = body_of(rest);
                fn->u.lambda.env = env;
                env_def(env, target->u.pair.car->u.sym, fn);
                return nil_obj();
            }
            printf("scheme: bad define\n");
            return NULL;
        }
        if (h == S_lambda) {
            Obj *params = e->u.pair.cdr->u.pair.car;
            Obj *body = e->u.pair.cdr->u.pair.cdr;   /* (body...) */
            int n = 0;
            for (Obj *p = params; p->type == T_PAIR; p = p->u.pair.cdr) n++;
            char **names = (char **)malloc(sizeof(char *) * n);
            int i = 0;
            for (Obj *p = params; p->type == T_PAIR; p = p->u.pair.cdr)
                names[i++] = p->u.pair.car->u.sym;
            Obj *fn = obj_new(T_LAMBDA);
            fn->u.lambda.params = names;
            fn->u.lambda.nparams = n;
            fn->u.lambda.body = body_of(body);
            fn->u.lambda.env = env;                   /* 闭包: 捕获定义环境 */
            return fn;
        }
        if (h == S_set) {
            Obj *name = e->u.pair.cdr->u.pair.car;
            Obj *val = eval(e->u.pair.cdr->u.pair.cdr->u.pair.car, env, 0);
            if (!val) return NULL;
            if (name->type != T_SYM || !env_set(env, name->u.sym, val)) {
                printf("scheme: set! unbound: %s\n", name->type == T_SYM ? name->u.sym : "?");
                return NULL;
            }
            return nil_obj();
        }
        if (h == S_cond) {
            Obj *clauses = e->u.pair.cdr;
            int matched = 0;
            for (; clauses->type == T_PAIR; clauses = clauses->u.pair.cdr) {
                Obj *cl = clauses->u.pair.car;
                Obj *test = cl->u.pair.car;
                if (test->type == T_SYM && test->u.sym == S_else) {
                    Obj *body = cl->u.pair.cdr;
                    while (body->type == T_PAIR && body->u.pair.cdr->type == T_PAIR) {
                        Obj *r = eval(body->u.pair.car, env, 0);
                        if (!r) return NULL;
                        body = body->u.pair.cdr;
                    }
                    e = body->u.pair.car; matched = 1; break;   /* 尾位置 */
                }
                Obj *cv = eval(test, env, 0);
                if (!cv) return NULL;
                int truth = !(cv->type == T_BOOL && cv->u.num == 0);
                if (truth) {
                    Obj *body = cl->u.pair.cdr;
                    if (body->type == T_NIL) return boolean(1);
                    while (body->type == T_PAIR && body->u.pair.cdr->type == T_PAIR) {
                        Obj *r = eval(body->u.pair.car, env, 0);
                        if (!r) return NULL;
                        body = body->u.pair.cdr;
                    }
                    e = body->u.pair.car; matched = 1; break;   /* 尾位置 */
                }
            }
            if (matched) { tail = 1; continue; }     /* 命中: 尾位置走主循环 */
            return nil_obj();                         /* 无子句命中 */
        }
        if (h == S_let) {
            Obj *binds = e->u.pair.cdr->u.pair.car;
            Obj *body = e->u.pair.cdr->u.pair.cdr;
            Env *n = env_new(env);
            for (; binds->type == T_PAIR; binds = binds->u.pair.cdr) {
                Obj *b = binds->u.pair.car;          /* (name expr) */
                Obj *val = eval(b->u.pair.cdr->u.pair.car, env, 0);
                if (!val) return NULL;
                env_def(n, b->u.pair.car->u.sym, val);
            }
            e = body_of(body);
            env = n;
            tail = 1;                                /* TCO: 尾位置 */
            continue;
        }
        if (h == S_and) {
            Obj *args = e->u.pair.cdr;
            if (args->type == T_NIL) return boolean(1);
            for (; args->u.pair.cdr->type == T_PAIR; args = args->u.pair.cdr) {
                Obj *v = eval(args->u.pair.car, env, 0);
                if (!v) return NULL;
                if (v->type == T_BOOL && v->u.num == 0) return boolean(0);
            }
            e = args->u.pair.car; tail = 1; continue; /* 最后一项尾位置 */
        }
        if (h == S_or) {
            Obj *args = e->u.pair.cdr;
            if (args->type == T_NIL) return boolean(0);
            for (; args->u.pair.cdr->type == T_PAIR; args = args->u.pair.cdr) {
                Obj *v = eval(args->u.pair.car, env, 0);
                if (!v) return NULL;
                if (!(v->type == T_BOOL && v->u.num == 0)) return v;
            }
            e = args->u.pair.car; tail = 1; continue; /* 最后一项尾位置 */
        }

        /* ---- 普通函数调用 (h 是函数名) ---- */
        Obj *fn = eval(head, env, 0);
        if (!fn) return NULL;
        Obj *args = eval_list(e->u.pair.cdr, env);
        if (!args) return NULL;
        if (fn->type == T_BUILTIN)
            return fn->u.builtin(args);
        if (fn->type == T_LAMBDA) {
            Env *n = env_new(fn->u.lambda.env);
            Obj *a = args;
            for (int i = 0; i < fn->u.lambda.nparams; i++) {
                if (a->type == T_NIL) break;
                env_def(n, fn->u.lambda.params[i], a->u.pair.car);
                a = a->u.pair.cdr;
            }
            if (tail) { e = fn->u.lambda.body; env = n; continue; }  /* TCO! */
            return eval(fn->u.lambda.body, n, 0);
        }
        printf("scheme: not a function\n");
        return NULL;
    }
}

/* ================= 打印 ================= */
static void print_obj(Obj *o) {
    if (!o) { printf("<err>"); return; }
    switch (o->type) {
    case T_NUM: printf("%ld", o->u.num); break;
    case T_SYM: printf("%s", o->u.sym); break;
    case T_STR: printf("\"%s\"", o->u.str); break;
    case T_BOOL: printf(o->u.num ? "#t" : "#f"); break;
    case T_NIL: printf("()"); break;
    case T_BUILTIN: printf("<builtin>"); break;
    case T_LAMBDA: printf("<lambda>"); break;
    case T_PAIR: {
        printf("(");
        Obj *p = o;
        for (;;) {
            print_obj(p->u.pair.car);
            if (p->u.pair.cdr->type == T_NIL) break;
            if (p->u.pair.cdr->type != T_PAIR) {   /* 点对 */
                printf(" . ");
                print_obj(p->u.pair.cdr);
                break;
            }
            printf(" ");
            p = p->u.pair.cdr;
        }
        printf(")");
        break;
    }
    }
}

/* ================= 内建函数 ================= */
static Obj *arg1(Obj *args) { return args->u.pair.car; }

static Obj *bi_add(Obj *a) { long s = 0; for (; a->type == T_PAIR; a = a->u.pair.cdr) s += a->u.pair.car->u.num; return num(s); }
static Obj *bi_sub(Obj *a) {
    long s = a->u.pair.car->u.num;
    a = a->u.pair.cdr;
    if (a->type == T_NIL) return num(-s);
    for (; a->type == T_PAIR; a = a->u.pair.cdr) s -= a->u.pair.car->u.num;
    return num(s);
}
static Obj *bi_mul(Obj *a) { long s = 1; for (; a->type == T_PAIR; a = a->u.pair.cdr) s *= a->u.pair.car->u.num; return num(s); }
static Obj *bi_div(Obj *a) {
    long s = a->u.pair.car->u.num;
    a = a->u.pair.cdr;
    for (; a->type == T_PAIR; a = a->u.pair.cdr) {
        long d = a->u.pair.car->u.num;
        if (d == 0) { printf("scheme: divide by zero\n"); return NULL; }
        s /= d;
    }
    return num(s);
}
static int lt(long x, long y) { return x < y; }
static int gt(long x, long y) { return x > y; }
static int le(long x, long y) { return x <= y; }
static int ge(long x, long y) { return x >= y; }
static int eq(long x, long y) { return x == y; }

static Obj *bi_cmp(Obj *a, int (*f)(long, long)) {
    long prev = a->u.pair.car->u.num;
    for (a = a->u.pair.cdr; a->type == T_PAIR; a = a->u.pair.cdr) {
        long cur = a->u.pair.car->u.num;
        if (!f(prev, cur)) return boolean(0);
        prev = cur;
    }
    return boolean(1);
}
static Obj *bi_eq_n(Obj *a) { return bi_cmp(a, eq); }
static Obj *bi_lt_n(Obj *a) { return bi_cmp(a, lt); }
static Obj *bi_gt_n(Obj *a) { return bi_cmp(a, gt); }
static Obj *bi_le_n(Obj *a) { return bi_cmp(a, le); }
static Obj *bi_ge_n(Obj *a) { return bi_cmp(a, ge); }

static Obj *bi_car(Obj *a) { Obj *p = a->u.pair.car; return p->u.pair.car; }
static Obj *bi_cdr(Obj *a) { Obj *p = a->u.pair.car; return p->u.pair.cdr; }
static Obj *bi_cons(Obj *a) {
    return cons(a->u.pair.car, a->u.pair.cdr->u.pair.car);
}
static Obj *bi_null(Obj *a)  { return boolean(a->u.pair.car->type == T_NIL); }
static Obj *bi_pair(Obj *a)  { return boolean(a->u.pair.car->type == T_PAIR); }
static Obj *bi_number(Obj *a){ return boolean(a->u.pair.car->type == T_NUM); }
static Obj *bi_symbol(Obj *a){ return boolean(a->u.pair.car->type == T_SYM); }
static Obj *bi_string(Obj *a){ return boolean(a->u.pair.car->type == T_STR); }
static Obj *bi_boolean(Obj *a){ return boolean(a->u.pair.car->type == T_BOOL); }
static Obj *bi_not(Obj *a)   {
    Obj *v = a->u.pair.car;
    return boolean(v->type == T_BOOL && v->u.num == 0);
}
static Obj *bi_eq(Obj *a) {
    Obj *x = a->u.pair.car, *y = a->u.pair.cdr->u.pair.car;
    if (x->type == T_NUM && y->type == T_NUM) return boolean(x->u.num == y->u.num);
    return boolean(x == y);   /* 符号/布尔/对象指针 */
}
static Obj *bi_list(Obj *a) { return a; }
static Obj *bi_display(Obj *a) {
    /* 字符串原样输出(不带引号), 其他类型走 print_obj */
    Obj *v = a->u.pair.car;
    if (v->type == T_STR) printf("%s", v->u.str);
    else print_obj(v);
    return nil_obj();
}
static Obj *bi_newline(Obj *a) { printf("\n"); return nil_obj(); }
static Obj *bi_exit(Obj *a) {
    exit(a->type == T_PAIR && a->u.pair.car->type == T_NUM ? (int)a->u.pair.car->u.num : 0);
    return nil_obj();
}

static void install_builtin(Env *env, char *name, Obj *(*fn)(Obj *)) {
    Obj *o = obj_new(T_BUILTIN);
    o->u.builtin = fn;
    env_def(env, name, o);
}

static Env *global_env(void) {
    Env *e = env_new(NULL);
    g_global = e;              /* GC 根: 每次 gc_collect 从它 mark */
    install_builtin(e, "+", bi_add);
    install_builtin(e, "-", bi_sub);
    install_builtin(e, "*", bi_mul);
    install_builtin(e, "/", bi_div);
    install_builtin(e, "=", bi_eq_n);
    install_builtin(e, "<", bi_lt_n);
    install_builtin(e, ">", bi_gt_n);
    install_builtin(e, "<=", bi_le_n);
    install_builtin(e, ">=", bi_ge_n);
    install_builtin(e, "car", bi_car);
    install_builtin(e, "cdr", bi_cdr);
    install_builtin(e, "cons", bi_cons);
    install_builtin(e, "null?", bi_null);
    install_builtin(e, "pair?", bi_pair);
    install_builtin(e, "number?", bi_number);
    install_builtin(e, "symbol?", bi_symbol);
    install_builtin(e, "string?", bi_string);
    install_builtin(e, "boolean?", bi_boolean);
    install_builtin(e, "not", bi_not);
    install_builtin(e, "eq?", bi_eq);
    install_builtin(e, "list", bi_list);
    install_builtin(e, "display", bi_display);
    install_builtin(e, "newline", bi_newline);
    install_builtin(e, "exit", bi_exit);
    return e;
}

/* ================= REPL / main ================= */
static char linebuf[8192];
static int  linebuf_len = 0;

/* 读完整表达式(跨行直到括号平衡), 返回 malloc 的串 */
static char *read_full(void) {
    int cap = 8192, len = 0;
    char *buf = (char *)malloc(cap);
    for (;;) {
        char tmp[512];
        if (!fgets(tmp, sizeof(tmp), stdin)) break;
        int n = strlen(tmp);
        if (len + n + 1 > cap) {
            cap *= 2;
            char *nb = (char *)malloc(cap);
            strcpy(nb, buf);
            free(buf);
            buf = nb;
        }
        strcpy(buf + len, tmp);
        len += n;
        if (balanced(buf)) break;
    }
    if (len == 0) { free(buf); return NULL; }
    return buf;
}

static void repl(Env *env) {
    printf("RmikuScheme 0.1 — type expressions, Ctrl+D exits\n");
    for (;;) {
        printf("> ");
        char *src = read_full();
        if (!src) { printf("\nbye~\n"); break; }
        src_pos = 0;
        while (1) {
            Obj *expr = read_expr(src);
            if (!expr) break;
            gc_root_expr = expr;                 /* GC 根 */
            Obj *r = eval(expr, env, 0);
            gc_root_expr = NULL;
            if (r) { print_obj(r); printf("\n"); }
            /* 一行可能有多个表达式(如 (define...) (define...))——循环 */
            if (src[src_pos] == '\0') break;
        }
        free(src);
    }
}

static int run_file(Env *env, char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        printf("scheme: cannot open %s\n", path);
        return 1;
    }
    /* 读全部(最多 256KB, 教学够用) */
    static char src[262144];
    size_t len = fread(src, 1, sizeof(src) - 1, fp);
    fclose(fp);
    src[len] = '\0';
    src_pos = 0;
    for (;;) {
        Obj *expr = read_expr(src);
        if (!expr) break;
        gc_root_expr = expr;                 /* GC 根 */
        Obj *r = eval(expr, env, 0);
        gc_root_expr = NULL;
        if (!r) return 1;
        if (src[src_pos] == '\0') break;
    }
    return 0;
}

int main(int argc, char **argv) {
    init_special_symbols();      /* 特殊形式名预 intern, eval 按指针比较 */
    Env *env = global_env();
    if (argc >= 2)
        return run_file(env, argv[1]);
    repl(env);
    return 0;
}
