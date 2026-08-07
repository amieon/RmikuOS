/* t02: 字符串库——strlen/strcpy/strcat/strcmp/strchr/strstr */
#include "user.h"

int main() {
    char buf[64];
    strcpy(buf, "Hello");
    strcat(buf, ", RmikuOS");
    strcat(buf, "!");
    printf("buf = [%s]  len = %d\n", buf, strlen(buf));
    printf("strcmp(abc, abd) = %d\n", strcmp("abc", "abd"));
    printf("strcmp(abc, abc) = %d\n", strcmp("abc", "abc"));
    char *p = strchr(buf, 'R');
    printf("strchr(R) -> %s\n", p ? p : "(null)");
    char *q = strstr(buf, "miku");
    printf("strstr(miku) -> %s\n", q ? q : "(null)");
    char *r = strstr(buf, "zzz");
    printf("strstr(zzz) -> %s\n", r ? r : "(null)");
    printf("atoi(\"42xyz\") = %d, strtol = %d\n", atoi("42xyz"), (int)strtol("1a", 0, 16));
    return 0;
}
