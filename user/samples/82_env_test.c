#include "user.h"

/* 自带 strlen，避免依赖未知 libc 符号（与 shell.c 的 strlen_ 同理） */
static int __slen__(const char *s) {
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

/* 安全打印：getenv 返回共享静态缓冲，绝不在同一条 printf 里调用两次 getenv */
static void show(const char *key) {
    const char *v = getenv(key);
    if (v) printf("%s = %s\n", key, v);
    else   printf("%s = (null)\n", key);
}

int main(int argc, char *argv[], char *envp[]){
    printf("==== 1) 读内核 seed 的环境变量（init 时已注入） ====\n");
    show("PATH");          /* 期望 /bin:/tests:/programs */
    show("HOME");          /* 期望 /home */
    show("PWD");           /* 期望 / */

    printf("\n==== 2) 通过 main 第 3 参数传入的 environ[] ====\n");
    for (int i = 0; envp[i]; i++) {
        printf("  environ[%d] = %s\n", i, envp[i]);
    }

    printf("\n==== 3) setenv + getenv ====\n");
    setenv_s("MYVAR", "hello", 1);
    show("MYVAR");         /* 期望 hello */

    printf("\n==== 4) overwrite=0 不覆盖已存在的值 ====\n");
    setenv_s("MYVAR", "world", 0);
    show("MYVAR");         /* 期望仍为 hello */

    printf("\n==== 5) overwrite=1 覆盖 ====\n");
    setenv_s("MYVAR", "world", 1);
    show("MYVAR");         /* 期望 world */

    printf("\n==== 6) unsetenv ====\n");
    unsetenv_s("MYVAR");
    show("MYVAR");         /* 期望 (null) */

    printf("\n==== 7) listenv 枚举全部（KEY=VALUE\\0 串联） ====\n");
    char buf[2048];
    isize n = listenv(buf, (usize)sizeof(buf));
    printf("listenv 返回 %d 字节:\n", (int)n);
    int off = 0;
    while (off < n) {
        printf("  %s\n", buf + off);
        off += __slen__(buf + off) + 1;
    }

    printf("\n==== 8) clearenv 清空 ====\n");
    clearenv();
    show("PATH");          /* 期望 (null) */
    show("HOME");          /* 期望 (null) */

    printf("\n==== 9) clearenv 之后仍可 setenv ====\n");
    setenv_s("AFTER", "ok", 1);
    show("AFTER");         /* 期望 ok */

    printf("\n==== 全部用例完成 ====\n");
    return 0;
}
