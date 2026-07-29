// xx_groups.c — 验证附加组(supplementary groups)是否真正生效。
//
// 用法:
//   /samples/groups           打印当前凭证(uid/euid/gid/egid)与 getgroups 列表
//   /samples/groups <path>    尝试以读方式打开 path, 打印成功或 "Permission denied"
//
// 演示场景(在 shell 里由 root 准备):
//   mkdir /home/shared
//   echo secret > /home/shared/notes
//   chown root:staff /home/shared/notes
//   chmod 0640 /home/shared/notes
//   此时只有 staff 组( gid 50, 见 /etc/group 中 "staff:50:alice")的成员能读。
//   alice 以附加组 50 登录可成功打开; bob 不在 staff 组, 被拒绝。
//
// 说明: 本程序仅依赖用户态封装(getuid/geteuid/getgid/getegid/getgroups/open),
// 真正的权限判定在内核 check_access 中完成。

#include "user.h"
#include "string.h"

int main(int argc, char *argv[]) {
    usize uid  = (usize) getuid();
    usize euid = (usize) geteuid();
    usize gid  = (usize) getgid();
    usize egid = (usize) getegid();

    printf("uid=%d euid=%d gid=%d egid=%d\n", uid, euid, gid, egid);

    int n = (int) getgroups(0, 0);
    if (n < 0) {
        puts("getgroups: failed\n");
        return 1;
    }
    printf("ngroups=%d\n", n);
    if (n > 0) {
        usize list[32];
        if (getgroups((usize) n, list) == n) {
            for (int i = 0; i < n; i++) {
                printf("  group[%d] = %d\n", i, (int) list[i]);
            }
        } else {
            puts("getgroups: buffer mismatch\n");
        }
    }

    if (argc >= 2) {
        int fd = (int) open(argv[1], O_RDONLY);
        if (fd < 0) {
            printf("open(%s): Permission denied (or missing)\n", argv[1]);
            return 1;
        }
        puts("open: OK (group permission matched)\n");
        close(fd);
    }
    return 0;
}
