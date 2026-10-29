#include "test.h"
#include "dirent.h"

/* git 基础功能回归: init / add / commit / log / status / show
 *
 * 为什么单独立一个测试: 一次 `git commit` 是"多进程 + 大量文件系统元操作"的
 * 组合负载 —— fork/exec + pipe + open/read/write + mkdir + readdir + stat 比
 * mtime + lockfile 的 create/rename。这些接口单独测都过, 组合起来才会暴露
 * 顺序、errno、权限、路径映射上的缝隙(之前 git init 连挂三个坑, 就是这种)。
 * 所以这里不测 git 的语义细节, 只测"最基本的链路能跑通", 失败时靠哪一步红
 * 来定位。
 *
 * 除了退出码和文件状态, **stderr 单独判**: git 存在"子进程/子步骤失败, 但
 * 主命令退出码 0、结果也正确"的行为(commit 之后 spawn 的 maintenance 就是),
 * 那种缺陷只有 stderr 有痕迹。见 g_stderr_fatal 与第 9 节的注释。
 *
 * 环境依赖:
 *   - /bin/git。没有就 SKIP(77) —— 没测必须明说, 不能算 PASS。
 *   - /tmp 可写。工作目录 /tmp/git_basic/repo, 每次开头递归清空, 可重复运行。
 *   - 身份用**仓库级** user.name/user.email, 不碰 $HOME/.gitconfig。
 *   - 用 --template= 跳过模板复制: 模板是打包问题, 不是 git 功能, 不该让它
 *     决定这个测试的红绿。 */

#define GIT_BIN  "/bin/git"
#define ROOT_DIR "/tmp/git_basic"
#define REPO_DIR ROOT_DIR "/repo"
#define ERR_LOG  ROOT_DIR "/.git-stderr.log"
#define OUT_SZ   2048

/* 全程 stderr 的"致命输出"累计。
 *
 * 为什么必须单独抓 stderr: git 有一整类"子步骤失败但主命令成功"的行为 ——
 * `git commit` 提交完会同步 spawn `git maintenance run --auto`(run-command.c
 * 的 run_auto_maintenance), 那个子进程挂了只往 stderr 打一行 fatal, commit
 * 自己的退出码照样是 0、提交照样落盘。只重定向 stdout 的测试对这种缺陷完全瞎。
 * (之前 `fatal: cannot exec 'maintenance': Function not implemented` 就是这么
 * 混过去的 —— 提交成功, 但有一行 fatal 没人管。)
 *
 * 判定: fatal:/error: 判红; warning: 只统计不判红(环境差异常带来无害 warning)。
 */
static int  g_stderr_fatal = 0;
static int  g_stderr_warn_count = 0;
static char g_stderr_first[200];
static char g_stderr_cmd[80];
static char g_stderr_buf[1024];

/* ---- 小工具 ---- */

/* 递归删除。没有 rm 命令, 测试自己来。
 * 外层 for 是兜底: readdir 遍历中删条目可能让后续 getdents 跳过几个,
 * 那就再 opendir 一轮, 直到 rmdir 成功(目录真空)。guard 防死循环。 */
static void rm_tree(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return;
    if (!S_ISDIR(st.st_mode)) { unlink(path); return; }

    for (int guard = 0; guard < 8; guard++) {
        DIR *d = opendir(path);
        if (!d) break;
        struct dirent *e;
        while ((e = readdir(d)) != 0) {
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;

            char child[192];
            usize need = strlen(path) + 1 + strlen(e->d_name) + 1;
            if (need > sizeof(child)) continue;    /* 名字过长, 跳过(本测试不会出现) */

            strcpy(child, path);
            strcat(child, "/");
            strcat(child, e->d_name);
            rm_tree(child);
        }
        closedir(d);
        if (rmdir(path) == 0) return;
    }
}

/* 尾巴去空白(输出是 "\n" 结尾的一行, 断言要拿干净字符串比较) */
static void rstrip(char *s) {
    usize n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) {
        s[--n] = 0;
    }
}

static int count_lines(const char *s) {
    int n = 0;
    for (; *s; s++) if (*s == '\n') n++;
    return n;
}

static int is_hex40(const char *s) {
    if (strlen(s) != 40) return 0;
    for (int i = 0; i < 40; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return 0;
    }
    return 1;
}

static int write_file(const char *path, const char *data) {
    int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) return -1;
    isize n = write(fd, data, strlen(data));
    close(fd);
    return (n == (isize)strlen(data)) ? 0 : -1;
}

/* 整文件读进 buf, 返回字节数, 失败 -1。用于直接看 .git/HEAD 这种东西。 */
static int read_file(const char *path, char *buf, usize bufsz) {
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0) return -1;
    isize n = read(fd, buf, bufsz - 1);
    close(fd);
    if (n < 0) return -1;
    buf[n] = 0;
    return (int)n;
}

/* 跑一次 git, **stdout** 收进 out, **stderr** 收到 ERR_LOG 单独检查,
 * 返回退出码(不是 wait status)。-1 = 起不来。
 *
 * 四个必须注意的点:
 *   1. 先读到 EOF 再 waitpid。反过来: 子进程写满管道缓冲区会阻塞, 父进程
 *      又在 waitpid 等它退出 —— 死锁。所以边读边丢(缓冲满就只读不存)。
 *   2. 退出码要过 WIFEXITED/WEXITSTATUS。内核写回的是 POSIX wait status
 *      (正常退出是 code<<8), 直接比 77/127 永远不成立。
 *   3. stdout 和 stderr 必须**分开**: 合流会让 "status 输出为空" 这种断言
 *      被一行 warning 带偏; 但丢掉 stderr 又会漏掉"主命令成功、子步骤报
 *      fatal"的情况(见 g_stderr_fatal 的注释)。所以 stderr 落到文件, 单独判。
 *   4. 用文件而不是第二条管道: 两个管道要并发读才不会死锁, 这个测试不需要
 *      实时性, 文件最简单可靠。run_auto_maintenance 是同步 run_command,
 *      所以 waitpid 返回时 git 及其维护子进程的 stderr 都已落盘。 */
static int run_git(const char *const av[], int ac, char *out, usize outsz) {
    if (ac <= 0 || ac > EXEC_MAX_ARGS) return -1;

    int fds[2];
    if (pipe(fds) != 0) return -1;

    /* 先开 err 文件再 fork: 子进程靠继承拿 fd, fork 后父子各自 close。 */
    int efd = open(ERR_LOG, O_CREAT | O_WRONLY | O_TRUNC, 0644);

    int pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        if (efd >= 0) close(efd);
        return -1;
    }

    if (pid == 0) {
        close(fds[0]);
        dup2(fds[1], 1);
        if (fds[1] > 2) close(fds[1]);
        if (efd >= 0) {
            dup2(efd, 2);
            if (efd > 2) close(efd);
        }

        struct exec_args args;
        args.argc = (usize)ac;
        for (int i = 0; i < EXEC_MAX_ARGS; i++) {
            args.argv[i].ptr = 0;
            args.argv[i].len = 0;
        }
        for (int i = 0; i < ac; i++) {
            args.argv[i].ptr = av[i];
            args.argv[i].len = strlen(av[i]);
        }
        exec_with_args(GIT_BIN, &args);
        _exit(127);          /* exec 失败才会到这里 */
    }

    close(fds[1]);
    if (efd >= 0) close(efd);        /* 父进程这份副本没用, 留着会让子进程写不完 */

    usize used = 0;
    char tmp[256];
    isize n;
    while ((n = read(fds[0], tmp, sizeof(tmp))) > 0) {
        if (out && outsz > 1 && used + 1 < outsz) {
            usize room = outsz - 1 - used;
            usize take = ((usize)n < room) ? (usize)n : room;
            memcpy(out + used, tmp, take);
            used += take;
        }
        /* 缓冲满了继续读但丢弃 —— 不能 break, 否则死锁(见函数头注释) */
    }
    if (out && outsz > 0) out[used] = 0;
    close(fds[0]);

    int status = 0;
    isize r;
    do {
        r = waitpid(pid, &status, 0);
    } while (r < 0 && errno == EINTR);

    int rc;
    if (r != pid) {
        rc = -1;
    } else if (WIFEXITED(status)) {
        rc = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        rc = 128 + WTERMSIG(status);      /* 压平只在这里, 方便打印 */
    } else {
        rc = -1;
    }

    /* ---- 检查 stderr ---- */
    g_stderr_buf[0] = 0;
    if (efd >= 0 && read_file(ERR_LOG, g_stderr_buf, sizeof(g_stderr_buf)) < 0) {
        g_stderr_buf[0] = 0;
    }

    if (strstr(g_stderr_buf, "fatal:") || strstr(g_stderr_buf, "error:")) {
        printf("[git] stderr 有致命输出 (cmd=%s):\n%s\n",
               ac > 1 ? av[1] : "?", g_stderr_buf);
        if (!g_stderr_fatal) {
            g_stderr_fatal = 1;      /* 只留第一次的现场, 后面重复的会一直打但不必存 */
            strncpy(g_stderr_first, g_stderr_buf, sizeof(g_stderr_first) - 1);
            g_stderr_first[sizeof(g_stderr_first) - 1] = 0;
            strncpy(g_stderr_cmd, ac > 1 ? av[1] : "?", sizeof(g_stderr_cmd) - 1);
            g_stderr_cmd[sizeof(g_stderr_cmd) - 1] = 0;
        }
    }
    if (strstr(g_stderr_buf, "warning:")) g_stderr_warn_count++;

    /* 非 0 退出时把命令和已收到的 stdout 打出来: 断言只会报"期望 X 实际 Y",
     * 有这一行才能知道是哪条 git 命令、它说了什么。 */
    if (rc != 0) {
        printf("[git] rc=%d cmd: %s %s %s\n", rc, av[0],
               ac > 1 ? av[1] : "", ac > 2 ? av[2] : "");
        if (out && out[0]) printf("[git] stdout: %s\n", out);
    }
    return rc;
}

int main() {
    TEST_START("git_basic");

    /* ---- 0. 前置: 没有 git 就 SKIP ---- */
    if (access(GIT_BIN, X_OK) != 0) {
        puts("[SKIP] /bin/git 不存在或不可执行, 本架构未构建 git");
        return 77;      /* 77 = SKIP(与 run_all 约定一致): 没测, 不算 PASS */
    }
    CHECK(1, "前置: /bin/git 可执行");

    /* 不读宿主的 /etc/gitconfig 和 ~/.gitconfig, 保证结果只取决于本仓库 */
    setenv("GIT_CONFIG_NOSYSTEM", "1", 1);
    setenv("GIT_CONFIG_GLOBAL", "/dev/null", 1);
    setenv("GIT_PAGER", "cat", 1);

    char out[OUT_SZ];

    /* ---- 1. 准备干净的工作目录 ---- */
    rm_tree(ROOT_DIR);
    CHECK_EQ(mkdir(ROOT_DIR, 0755), 0, "准备: mkdir " ROOT_DIR);
    CHECK_EQ(mkdir(REPO_DIR, 0755), 0, "准备: mkdir " REPO_DIR);
    CHECK_EQ(chdir(REPO_DIR), 0, "准备: chdir 进仓库目录");

    /* ---- 2. git init ---- */
    {
        const char *av[] = { "git", "init", "-q", "-b", "main", "--template=" };
        CHECK_EQ(run_git(av, 6, out, sizeof(out)), 0, "git init 退出码 0");
    }

    struct stat st;
    CHECK(stat(REPO_DIR "/.git", &st) == 0 && S_ISDIR(st.st_mode), "init 后 .git 是目录");
    CHECK(stat(REPO_DIR "/.git/objects", &st) == 0 && S_ISDIR(st.st_mode), "存在 .git/objects/");
    CHECK(stat(REPO_DIR "/.git/refs/heads", &st) == 0 && S_ISDIR(st.st_mode), "存在 .git/refs/heads/");
    CHECK(stat(REPO_DIR "/.git/config", &st) == 0 && S_ISREG(st.st_mode), "存在 .git/config");

    /* HEAD 是符号引用。这一条同时验证: 写文件 + -b main 生效 + 换行符没被吞。 */
    if (read_file(REPO_DIR "/.git/HEAD", out, sizeof(out)) > 0) {
        rstrip(out);
        CHECK_STREQ(out, "ref: refs/heads/main", ".git/HEAD 指向 refs/heads/main");
    } else {
        FAIL("读不到 .git/HEAD");
    }

    /* ---- 3. 配置身份(仓库级, 不污染 $HOME) ---- */
    {
        const char *av[] = { "git", "config", "user.name", "Rmiku Test" };
        CHECK_EQ(run_git(av, 4, out, sizeof(out)), 0, "git config user.name");
    }
    {
        const char *av[] = { "git", "config", "user.email", "test@rmikuos.local" };
        CHECK_EQ(run_git(av, 4, out, sizeof(out)), 0, "git config user.email");
    }
    /* 这里**故意不设 `maintenance.auto=false`**。
     *
     * 设了就等于把 commit 之后那次 `git maintenance run --auto` 从测试路径里
     * 删掉(builtin/gc.c:1882 见到 false 直接 return 0), 而那正是之前唯一
     * 暴露出 "fatal: cannot exec 'maintenance'" 的地方 —— commit 成功、退出码 0,
     * 全靠 stderr 才能发现问题。测试要跑的是用户真实使用的那条路, 不是被
     * 改过配置的简化版。 */
    {
        char cfg[512];
        if (read_file(REPO_DIR "/.git/config", cfg, sizeof(cfg)) > 0) {
            CHECK(strstr(cfg, "maintenance.auto") == 0,
                  ".git/config 里没有 maintenance.auto(保持 git 默认 true)");
            CHECK(strstr(cfg, "test@rmikuos.local") != 0,
                  ".git/config 里确实写进了 user.email");
        } else {
            FAIL("读不到 .git/config");
        }
    }
    /* 读回: 这一步走的是"lockfile 写完 rename 成 .git/config"的完整写路径 */
    {
        const char *av[] = { "git", "config", "--get", "user.email" };
        int rc = run_git(av, 4, out, sizeof(out));
        rstrip(out);
        CHECK_EQ(rc, 0, "git config --get 退出码 0");
        CHECK_STREQ(out, "test@rmikuos.local", "读回 user.email 等于写入值");
    }

    /* ---- 4. add ---- */
    CHECK_EQ(write_file(REPO_DIR "/a.txt", "hello git\n"), 0, "写入 a.txt");

    {
        const char *av[] = { "git", "status", "--porcelain" };
        run_git(av, 3, out, sizeof(out));
        rstrip(out);
        CHECK(strstr(out, "?? a.txt") != 0, "add 前 status 报告 ?? a.txt(未跟踪)");
    }

    {
        const char *av[] = { "git", "add", "a.txt" };
        CHECK_EQ(run_git(av, 3, out, sizeof(out)), 0, "git add a.txt 退出码 0");
    }

    /* st 复用: 先算出是否 stat 成功, 再做第二条断言 ——
     * 否则 stat 失败时 st.st_size 是未初始化值, 断言会靠运气过。 */
    {
        int idx_ok = (stat(REPO_DIR "/.git/index", &st) == 0 && S_ISREG(st.st_mode));
        CHECK(idx_ok, "add 后生成 .git/index");
        CHECK(idx_ok && st.st_size > 0, ".git/index 非空");
    }

    /* porcelain v1 的格式是 "XY path": 已暂存新增 = "A " + " a.txt" */
    {
        const char *av[] = { "git", "status", "--porcelain" };
        run_git(av, 3, out, sizeof(out));
        rstrip(out);
        CHECK_STREQ(out, "A  a.txt", "add 后 status 是 'A  a.txt'");
    }

    {
        const char *av[] = { "git", "ls-files" };
        run_git(av, 2, out, sizeof(out));
        rstrip(out);
        CHECK_STREQ(out, "a.txt", "ls-files 列出 a.txt");
    }

    /* ---- 5. commit ---- */
    {
        const char *av[] = { "git", "commit", "-m", "first" };
        CHECK_EQ(run_git(av, 4, out, sizeof(out)), 0, "git commit -m first 退出码 0");
    }

    CHECK(stat(REPO_DIR "/.git/refs/heads/main", &st) == 0 && S_ISREG(st.st_mode),
          "commit 后 refs/heads/main 是普通文件(非 packed-refs)");

    char head_sha[64] = { 0 };
    {
        const char *av[] = { "git", "rev-parse", "HEAD" };
        run_git(av, 3, out, sizeof(out));
        rstrip(out);
        CHECK(is_hex40(out), "rev-parse HEAD 是 40 位十六进制");
        strncpy(head_sha, out, sizeof(head_sha) - 1);
    }

    /* refs 文件内容必须就是 rev-parse 出来的 sha —— 验证"引用写入"和"对象名"
     * 两条路径一致(lockfile rename 没写坏、没写到别的路径去) */
    {
        char ref[64];
        if (read_file(REPO_DIR "/.git/refs/heads/main", ref, sizeof(ref)) > 0) {
            rstrip(ref);
            CHECK_STREQ(ref, head_sha, "refs/heads/main 内容 == rev-parse HEAD");
        } else {
            FAIL("读不到 refs/heads/main");
        }
    }

    {
        const char *av[] = { "git", "log", "--oneline" };
        run_git(av, 3, out, sizeof(out));
        CHECK(strstr(out, "first") != 0, "log 里能看到提交说明 first");
        CHECK_EQ(count_lines(out), 1, "log 只有 1 条提交");
    }

    /* 最值钱的一条: 提交后工作区必须干净。
     * 它要求 index 里记的 stat(mtime/size) 与真实文件一致 —— 一旦内核 stat
     * 或 index 写入有偏差, git 就会把"刚提交的东西"报成已修改。 */
    {
        const char *av[] = { "git", "status", "--porcelain" };
        run_git(av, 3, out, sizeof(out));
        rstrip(out);
        CHECK_STREQ(out, "", "commit 后 status 干净(无输出)");
    }

    /* ---- 6. 第二次提交: 改已有文件 + 加新文件 ---- */
    CHECK_EQ(write_file(REPO_DIR "/a.txt", "hello git\nsecond line\n"), 0, "改写 a.txt");
    CHECK_EQ(write_file(REPO_DIR "/b.txt", "bee\n"), 0, "写入 b.txt");

    {
        const char *av[] = { "git", "status", "--porcelain" };
        run_git(av, 3, out, sizeof(out));
        CHECK(strstr(out, " M a.txt") != 0, "status 报告 M a.txt(已修改)");
        CHECK(strstr(out, "?? b.txt") != 0, "status 报告 ?? b.txt(未跟踪)");
    }

    {
        const char *av[] = { "git", "add", "." };
        CHECK_EQ(run_git(av, 3, out, sizeof(out)), 0, "git add . 退出码 0");
    }
    {
        const char *av[] = { "git", "commit", "-m", "second" };
        CHECK_EQ(run_git(av, 4, out, sizeof(out)), 0, "git commit -m second 退出码 0");
    }

    {
        const char *av[] = { "git", "log", "--oneline" };
        run_git(av, 3, out, sizeof(out));
        CHECK_EQ(count_lines(out), 2, "log 现在有 2 条提交");
    }
    {
        const char *av[] = { "git", "log", "-1", "--pretty=%s" };
        run_git(av, 4, out, sizeof(out));
        rstrip(out);
        CHECK_STREQ(out, "second", "log -1 --pretty=%s 是 second(最新提交在前)");
    }

    /* 父提交链: HEAD~1 只能看到 1 条 */
    {
        const char *av[] = { "git", "log", "--oneline", "HEAD~1" };
        int rc = run_git(av, 4, out, sizeof(out));
        CHECK_EQ(rc, 0, "log HEAD~1 退出码 0");
        CHECK_EQ(count_lines(out), 1, "HEAD~1 只有 1 条提交(parent 链正确)");
    }

    /* ---- 7. 读回内容: 验证 object 写入 + zlib 往返 ---- */
    {
        const char *av[] = { "git", "show", "HEAD:a.txt" };
        int rc = run_git(av, 3, out, sizeof(out));
        CHECK_EQ(rc, 0, "git show HEAD:a.txt 退出码 0");
        CHECK_STREQ(out, "hello git\nsecond line\n", "HEAD:a.txt 内容与写入一致(压缩/解压无损)");
    }
    {
        const char *av[] = { "git", "ls-tree", "-r", "HEAD", "--name-only" };
        int rc = run_git(av, 5, out, sizeof(out));
        CHECK_EQ(rc, 0, "git ls-tree -r HEAD 退出码 0");
        CHECK(strstr(out, "a.txt") != 0, "tree 里含 a.txt");
        CHECK(strstr(out, "b.txt") != 0, "tree 里含 b.txt");
    }

    /* ---- 8. 收尾: 保持干净 ---- */
    {
        const char *av[] = { "git", "status", "--porcelain" };
        run_git(av, 3, out, sizeof(out));
        rstrip(out);
        CHECK_STREQ(out, "", "第二次 commit 后 status 干净");
    }

    /* ---- 9. 总闸: 全程 stderr 不能出现 fatal:/error: ----
     *
     * 这条专门抓"主命令成功、但内部子进程报致命错误"的那一类:
     * 退出码是 0, 文件也确实写出来了, 功能看起来全都对 —— 只有 stderr 留了痕迹。
     * `git commit` 提交完 spawn 维护子进程失败就是这样(commit.c 根本不看
     * run_auto_maintenance 的返回值)。单看退出码和文件状态, 这种缺陷永远漏。
     *
     * 放在最后而不是每条命令后面: 一个总闸就够, 且不打断上面的断言顺序;
     * 真红的时候 [git] 那几行会指出是哪条命令、说了什么。 */
    if (g_stderr_warn_count > 0) {
        printf("[git] 提示: 全程有 %d 次调用在 stderr 上打了 warning(不判红)\n",
               g_stderr_warn_count);
    }
    CHECK(!g_stderr_fatal, "全程 stderr 没有 fatal:/error:");
    if (g_stderr_fatal) {
        printf("[git] 首次致命输出来自 `%s`, 原文:\n%s\n", g_stderr_cmd, g_stderr_first);
    }

    /* 现场保留在 /tmp/git_basic/repo: 红了可以直接进去手敲, 下次运行开头会被清掉。 */

    TEST_END();
}
