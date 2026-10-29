#include "test.h"
#include "dirent.h"

/* git push 回归: file:// 传输
 *
 * 为什么单独一个测试, 不并进 git_basic:
 *
 * push 是唯一一条会**反向 fork 一个 git 服务端**的路径 ——
 * `git push file:///path` 在本进程里拼出 `git-receive-pack '/path'` 交给 shell
 * (connect.c:1439 start_command), 于是本地多出一个 git 进程, 客户端 send-pack.c
 * 通过两根 fd 跟它说 pkt-line、灌 packfile。这条链压的是 git_basic 完全没碰的
 * 一段: ref 的 lockfile→rename、pack 落盘、index-pack 回读、bad pack 清尾。
 *
 * 更关键的是它**不依赖网络**。所以这个测试红了, 问题一定在 push 逻辑或文件系统,
 * 不可能在 TCP —— 这是把"推不上 GitHub"这个问题切成两半最便宜的一刀。
 * 这一半通了, 换成 git:// 时上层代码一行不改(同一份 send_pack, 同样的协商字节),
 * 剩下那一半才真是网络栈的事。
 *
 * 环境依赖:
 *   - /bin/git
 *   - /bin/git-receive-pack: 必须**作为独立可执行文件存在**。connect.c 让 shell
 *     做 PATH 查找, 不认 `git receive-pack` 这种带空格的写法。mkfs_ext4.sh 把
 *     同一个 ELF 另存一份这个名字(git.c:880 会按 argv[0] 剥 "git-" 前缀直接调
 *     builtin), 所以它不是"第二个程序", 只是同一份二进制的另一个入口。
 *     缺它 / 不可执行 → SKIP(77)。没测必须明说, 不能算 PASS。
 *   - /tmp 可写。工作区 /tmp/git_push, 每次开头递归清空, 可重复运行。
 *
 * 为什么放在 /tmp 而不是 /data: 这里要测的是 push 逻辑, 不是 rsext4。挂两块盘
 * 进来(ext4-view + tmpfs)只会让红的时候多一个变量。反过来, 如果这里绿了而
 * 在 /data 上红, 那结论立刻就是"rsext4 的 rename/fsync/lockfile 有问题" ——
 * 分盘测反而把结论变干净了。 */

#define GIT_BIN    "/bin/git"
#define RECV_BIN   "/bin/git-receive-pack"
#define ROOT_DIR   "/tmp/git_push"
#define LOCAL_DIR  ROOT_DIR "/local"
#define REMOTE_DIR ROOT_DIR "/remote.git"
#define ERR_LOG    ROOT_DIR "/.git-stderr.log"
#define OUT_SZ     2048

/* "file://" + 绝对路径 = 三个斜杠。/tmp/git_push/remote.git 前面那个 / 就是
 * URL 里空的 authority 段(host 为空, path 从 / 开始), 不是写错了。 */
#define REMOTE_URL "file://" REMOTE_DIR

/* 首包 refspec: 推 HEAD 而不是推分支名。
 * `git push url main` 要求本地**正好**有个叫 main 的分支, 而 `HEAD:refs/heads/main`
 * 只要求有个 HEAD —— 少一个"本地分支名对不对"的失败维度, 红了不用先排查这个。 */
#define PUSH_REFSPEC "HEAD:refs/heads/main"

/* 全程 stderr 的致命输出累计。和 git_basic 同一套判据: fatal:/error: 判红,
 * warning: 只统计。push 的正常输出("To file://...", " * [new branch]")本来就
 * 走 stderr, 所以这里**不能**把"stderr 非空"当成失败, 只能抓关键字。 */
static int  g_stderr_fatal = 0;
static int  g_stderr_warn_count = 0;
static char g_stderr_first[400];
static char g_stderr_cmd[80];
static char g_stderr_buf[1024];
static int  g_trace = -1;

/* ---- 小工具(与 git_basic_test 同一套, 不共享头文件: 每个测试自包含) ---- */

/* 递归删除。没有 rm 命令, 测试自己来。
 * 外层 for 是兜底: readdir 遍历中删条目可能让后续 getdents 跳过几个, 那就再
 * opendir 一轮, 直到 rmdir 成功。guard 防死循环。 */
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
            if (need > sizeof(child)) continue;

            strcpy(child, path);
            strcat(child, "/");
            strcat(child, e->d_name);
            rm_tree(child);
        }
        closedir(d);
        if (rmdir(path) == 0) return;
    }
}

static void rstrip(char *s) {
    usize n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) {
        s[--n] = 0;
    }
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

/* 目录里有几个名字以 suffix 结尾的普通文件。用来确认 pack 真的落盘了
 * (objects/pack/pack-<sha>.pack), 而不是"看起来 push 成功了"。 */
static int count_files_with_suffix(const char *dir, const char *suffix) {
    DIR *d = opendir(dir);
    if (!d) return -1;

    int n = 0;
    usize slen = strlen(suffix);
    struct dirent *e;
    while ((e = readdir(d)) != 0) {
        usize nlen = strlen(e->d_name);
        if (nlen > slen && strcmp(e->d_name + nlen - slen, suffix) == 0) n++;
    }
    closedir(d);
    return n;
}

static int write_file(const char *path, const char *data) {
    int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) return -1;
    isize n = write(fd, data, strlen(data));
    close(fd);
    return (n == (isize)strlen(data)) ? 0 : -1;
}

static int read_file(const char *path, char *buf, usize bufsz) {
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0) return -1;
    isize n = read(fd, buf, bufsz - 1);
    close(fd);
    if (n < 0) return -1;
    buf[n] = 0;
    return (int)n;
}

/* 跑一次 git: stdout 收进 out, stderr 落到 ERR_LOG 单独检查, 返回退出码。
 * 四个注意点(理由同 git_basic_test, 这里不重复): 先读到 EOF 再 waitpid;
 * 退出码过 WIFEXITED; stdout/stderr 分开; 用文件而不是第二条管道。 */
static int run_git(const char *const av[], int ac, char *out, usize outsz) {
    if (ac <= 0 || ac > EXEC_MAX_ARGS) return -1;

    if (g_trace < 0) g_trace = getenv("RK_GIT_TRACE") ? 1 : 0;
    if (g_trace) {
        printf("[git] -> %s %s %s\n", av[0],
               ac > 1 ? av[1] : "", ac > 2 ? av[2] : "");
    }

    int fds[2];
    if (pipe(fds) != 0) return -1;

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
        _exit(127);
    }

    close(fds[1]);
    if (efd >= 0) close(efd);

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
        /* 缓冲满继续读但丢弃 —— 不能 break, 否则子进程写满管道后死锁 */
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
        rc = 128 + WTERMSIG(status);
    } else {
        rc = -1;
    }

    g_stderr_buf[0] = 0;
    if (efd >= 0 && read_file(ERR_LOG, g_stderr_buf, sizeof(g_stderr_buf)) < 0) {
        g_stderr_buf[0] = 0;
    }

    if (strstr(g_stderr_buf, "fatal:") || strstr(g_stderr_buf, "error:")) {
        printf("[git] stderr 有致命输出 (cmd=%s):\n%s\n",
               ac > 1 ? av[1] : "?", g_stderr_buf);
        if (!g_stderr_fatal) {
            g_stderr_fatal = 1;
            strncpy(g_stderr_first, g_stderr_buf, sizeof(g_stderr_first) - 1);
            g_stderr_first[sizeof(g_stderr_first) - 1] = 0;
            strncpy(g_stderr_cmd, ac > 1 ? av[1] : "?", sizeof(g_stderr_cmd) - 1);
            g_stderr_cmd[sizeof(g_stderr_cmd) - 1] = 0;
        }
    }
    if (strstr(g_stderr_buf, "warning:")) g_stderr_warn_count++;

    if (rc != 0) {
        printf("[git] rc=%d cmd: %s %s %s\n", rc, av[0],
               ac > 1 ? av[1] : "", ac > 2 ? av[2] : "");
        if (out && out[0]) printf("[git] stdout: %s\n", out);
        /* stderr 原文照打, 不只打"有致命输出"那种。
         * 上面那条只在含 fatal:/error: 时触发; 而"非 0 退出 + stderr 空"
         * 这种情况必须能看见 —— 不然只能靠猜是 fork 挂了还是 die 没退。 */
        if (g_stderr_buf[0]) printf("[git] stderr:\n%s\n", g_stderr_buf);
        else printf("[git] stderr: <空>\n");
    }
    return rc;
}

/* 让 argc 由数组自己算。手写数字在这个文件里踩过一次: `git init ... LOCAL_DIR`
 * 有 7 个参数, 照着 git_basic(那边 6 个, 靠 chdir 定位)抄成 6, 于是目标目录
 * 被吃掉, git 在 cwd 里初始化, 而 chdir 到不存在的目录 —— 两条断言都要红,
 * 但看不出根因。参数个数错一个都**不报编译错**, 只会安静地少传或多读。 */
#define RUN_GIT(av, out, outsz) \
    run_git((av), (int)(sizeof(av) / sizeof((av)[0])), (out), (outsz))

int main() {
    TEST_START("git_push");

    /* 版本戳：改过本文件后，这行时间必须变。没变就说明跑的是旧二进制。
     * 这个测试踩过两次"改了代码 -> 跑测试 -> 输出逐字相同"，只能靠
     * "某条 printf 没出现"反推，很容易把"没重编"误判成"改动没生效"。 */
    printf("[git_push] built %s %s\n", __DATE__, __TIME__);

    /* ---- 0. 前置 ---- */
    if (access(GIT_BIN, X_OK) != 0) {
        puts("[SKIP] /bin/git 不存在或不可执行, 本架构未构建 git");
        return 77;
    }
    if (access(RECV_BIN, X_OK) != 0) {
        puts("[SKIP] /bin/git-receive-pack 不存在或不可执行");
        puts("       file:// 传输靠 shell 按 PATH 找这个名字(不是 `git receive-pack`),");
        puts("       见 mkfs_ext4.sh 里把它和 git 二进制互为别名的那段。");
        return 77;
    }
    CHECK(1, "前置: /bin/git 与 /bin/git-receive-pack 均可执行");

    /* 不读宿主的 /etc/gitconfig 和 ~/.gitconfig, 结果只取决于这两个仓库 */
    setenv("GIT_CONFIG_NOSYSTEM", "1", 1);
    setenv("GIT_CONFIG_GLOBAL", "/dev/null", 1);
    setenv("GIT_PAGER", "cat", 1);

    char out[OUT_SZ];
    char head_sha[64] = {0};

    /* ---- 1. 准备 ---- */
    rm_tree(ROOT_DIR);
    CHECK_EQ(mkdir(ROOT_DIR, 0755), 0, "准备: mkdir " ROOT_DIR);

    /* ---- 2. 建裸远端 ----
     * bare: push 的目标必须是裸仓库。往非裸仓库的当前分支推会被
     * receive.denyCurrentBranch 拒掉(这是 git 的保护, 不是 bug)。
     *
     * 先跑参数矩阵再断言。起因: `git init --bare X` 报 rc=1 但 stderr 全空,
     * 而 cmd_init_db 里每条失败路径都是 die/die_errno/usage(129),
     * **没有一条是静默 return 1** —— 说明 rc=1 不是 init 自己返回的,
     * 而是 git.c 的 cmd_main 兜底那句 `return 1`。一次把组合全跑掉,
     * 直接二分出是 `-q` / `--bare` / `--template=` 哪一个, 比改一次跑一次快。 */
    {
        static const char *V[][8] = {
            { "git", "init",                 ROOT_DIR "/v1", 0 },
            { "git", "init", "-q",           ROOT_DIR "/v2", 0 },
            { "git", "init", "--bare",       ROOT_DIR "/v3", 0 },
            { "git", "init", "-q", "--bare", ROOT_DIR "/v4", 0 },
            { "git", "init", "-q", "--bare", "--template=", ROOT_DIR "/v5", 0 },
        };
        static const char *TAG[] = {
            "init DIR", "init -q DIR", "init --bare DIR",
            "init -q --bare DIR", "init -q --bare --template= DIR",
        };
        int n = (int)(sizeof(V) / sizeof(V[0])), i;
        puts("[git] init 参数矩阵(每行: 命令 / 退出码 / 目标目录有没有 HEAD):");
        for (i = 0; i < n; i++) {
            char o2[OUT_SZ], hpath[192];
            int ac = 0;
            struct stat t;
            while (ac < 8 && V[i][ac]) ac++;
            int rc = run_git(V[i], ac, o2, sizeof(o2));
            snprintf(hpath, sizeof(hpath), "%s/HEAD", V[i][ac - 1]);
            printf("       %-34s rc=%-4d %s\n", TAG[i], rc,
                   stat(hpath, &t) == 0 && S_ISREG(t.st_mode) ? "有 HEAD" :
                   (stat(V[i][ac - 1], &t) == 0 ? "目录在, 但没 HEAD" : "目录都没有"));
        }
    }

    {
        const char *av[] = { "git", "init", "-q", "--bare", "--template=", REMOTE_DIR };
        CHECK_EQ(RUN_GIT(av, out, sizeof(out)), 0, "git init --bare 远端退出码 0");
    }
    struct stat st;
    if (stat(REMOTE_DIR "/HEAD", &st) != 0) {
        /* 建不出来时把现场摊开。三种结论要能一眼分开:
         *   目录根本没有 / 目录在但是空的 / 有子目录但缺 HEAD。
         * 第一个说明 chdir 落到别处(bare 分支会 xgetcwd() 再 setenv GIT_DIR),
         * 第二个说明 init_db 半路退出, 第三个说明只有写文件那一步失败。 */
        printf("[git] %s 没建成, 目录内容:\n", REMOTE_DIR);
        DIR *dbg = opendir(REMOTE_DIR);
        if (!dbg) {
            printf("       <目录打不开, 可能连 mkdir 都没成>\n");
        } else {
            struct dirent *de;
            while ((de = readdir(dbg)) != 0) {
                if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
                printf("       %s\n", de->d_name);
            }
            closedir(dbg);
        }
        char cwdbuf[256];
        printf("[git] 当前 cwd = %s\n",
               getcwd(cwdbuf, sizeof(cwdbuf)) ? cwdbuf : "<getcwd 失败>");
    }
    CHECK(stat(REMOTE_DIR "/HEAD", &st) == 0 && S_ISREG(st.st_mode), "远端有 HEAD");
    CHECK(stat(REMOTE_DIR "/objects", &st) == 0 && S_ISDIR(st.st_mode), "远端有 objects/");
    CHECK(stat(REMOTE_DIR "/refs", &st) == 0 && S_ISDIR(st.st_mode), "远端有 refs/");

    /* 强制保留 pack, 不让 receive-pack 把它拆成松散对象。
     * 阈值是 receive.unpackLimit(默认 100), 对象少于它就 unpack。我们要测的
     * 恰恰是"二进制 pack 落盘 + 回读", 别让它走松散对象那条更简单的路。 */
    {
        const char *av[] = { "git", "--git-dir=" REMOTE_DIR, "config",
                             "receive.unpackLimit", "0" };
        CHECK_EQ(RUN_GIT(av, out, sizeof(out)), 0, "设远端 receive.unpackLimit=0");
    }

    /* ---- 3. 建本地仓库 ---- */
    {
        const char *av[] = { "git", "init", "-q", "-b", "main", "--template=", LOCAL_DIR };
        CHECK_EQ(RUN_GIT(av, out, sizeof(out)), 0, "git init 本地退出码 0");
    }
    CHECK_EQ(chdir(LOCAL_DIR), 0, "chdir 进本地仓库");

    {
        const char *av[] = { "git", "config", "user.name", "Rmiku Test" };
        CHECK_EQ(RUN_GIT(av, out, sizeof(out)), 0, "git config user.name");
    }
    {
        const char *av[] = { "git", "config", "user.email", "test@rmikuos.local" };
        CHECK_EQ(RUN_GIT(av, out, sizeof(out)), 0, "git config user.email");
    }

    /* ---- 4. 造第一个提交 ---- */
    CHECK_EQ(write_file(LOCAL_DIR "/a.txt", "push me\n"), 0, "写入 a.txt");
    {
        const char *av[] = { "git", "add", "a.txt" };
        CHECK_EQ(RUN_GIT(av, out, sizeof(out)), 0, "git add a.txt");
    }
    {
        const char *av[] = { "git", "commit", "-q", "-m", "first" };
        CHECK_EQ(RUN_GIT(av, out, sizeof(out)), 0, "git commit first");
    }
    {
        const char *av[] = { "git", "rev-parse", "HEAD" };
        int rc = RUN_GIT(av, out, sizeof(out));
        rstrip(out);
        CHECK_EQ(rc, 0, "git rev-parse HEAD 退出码 0");
        CHECK(is_hex40(out), "HEAD 是 40 位 sha1");
        if (is_hex40(out)) strcpy(head_sha, out);
    }

    /* ---- 5. 第一次 push (file://) ----
     *
     * 这一行背后发生的事: 池里 fork 出 `git-receive-pack '<REMOTE_DIR>'` →
     * 通过 pipe 收 pkt-line(`<old> <new> refs/heads/main\0report-status
     * side-band-64k`) → 收 pack → index-pack → 加锁写 ref → fsync。
     * 退出码 0 只代表"对面说 ok", 所以后面每一条都要去远端核实。 */
    {
        const char *av[] = { "git", "push", REMOTE_URL, PUSH_REFSPEC };
        int rc = RUN_GIT(av, out, sizeof(out));
        CHECK_EQ(rc, 0, "git push file:// 退出码 0");
    }

    /* ---- 6. 去远端核实(不看 push 自己的输出) ---- */
    {
        char refbuf[128];
        if (read_file(REMOTE_DIR "/refs/heads/main", refbuf, sizeof(refbuf)) > 0) {
            rstrip(refbuf);
            CHECK(is_hex40(refbuf), "远端 refs/heads/main 是 40 位 sha1");
            CHECK_STREQ(refbuf, head_sha, "远端 refs/heads/main == 本地 HEAD");
        } else {
            FAIL("远端不存在的 refs/heads/main(ref 没落地)");
        }
    }

    /* git 自己读一遍: 走完整 ref 解析, 比"文件内容对不对"更硬 */
    {
        const char *av[] = { "git", "--git-dir=" REMOTE_DIR, "rev-parse",
                             "--verify", "refs/heads/main" };
        int rc = RUN_GIT(av, out, sizeof(out));
        rstrip(out);
        CHECK_EQ(rc, 0, "远端 rev-parse --verify refs/heads/main 退出码 0");
        CHECK_STREQ(out, head_sha, "远端 rev-parse 得到的 sha == 本地 HEAD");
    }

    /* 对象真的能解出来: cat-file 会校验 sha1 并 zlib 解压。
     * 这一条才证明"pack 里的内容是对的", 光有 ref 只证明 ref 写对了。
     *
     * 这里**必须写 refs/heads/main 而不是 HEAD**: `git init --bare` 的 HEAD
     * 默认指向 refs/heads/master(和远端上实际存在的分支无关), 我们推的是
     * refs/heads/main。拿 HEAD 去问会撞 "ambiguous argument 'HEAD'" ——
     * 那是个假失败, 会把人骗去看 push 代码, 而 push 其实是对的。 */
    {
        const char *av[] = { "git", "--git-dir=" REMOTE_DIR, "cat-file", "-t",
                             "refs/heads/main" };
        int rc = RUN_GIT(av, out, sizeof(out));
        rstrip(out);
        CHECK_EQ(rc, 0, "远端 cat-file -t 退出码 0(对象可解压 + sha1 校验通过)");
        CHECK_STREQ(out, "commit", "远端 main 指向的对象类型是 commit");
    }

    /* pack 必须留在 objects/pack/ 里(unpackLimit=0 的效果)。
     * receive-pack 先写 tmp 再 rename, 所以这里看到 .pack 就说明那一对
     * create/rename 成功了; 只写了一半的半成品不会以 .pack 结尾。 */
    {
        int npack = count_files_with_suffix(REMOTE_DIR "/objects/pack", ".pack");
        CHECK(npack >= 1, "远端 objects/pack/ 里至少有 1 个 .pack");
        int nidx = count_files_with_suffix(REMOTE_DIR "/objects/pack", ".idx");
        CHECK(nidx >= 1, "远端 objects/pack/ 里至少有 1 个 .idx(index-pack 完成)");
    }

    /* ---- 7. 第二个提交 + 增量 push ----
     * 第二次走的是 fast-forward 分支: receive-pack 要先读远端已有的 ref,
     * 判断 old 是不是 new 的祖先。这一步在本地同样要做, 跟有没有网无关。 */
    CHECK_EQ(write_file(LOCAL_DIR "/b.txt", "second push\n"), 0, "写入 b.txt");
    {
        const char *av[] = { "git", "add", "b.txt" };
        CHECK_EQ(RUN_GIT(av, out, sizeof(out)), 0, "git add b.txt");
    }
    {
        const char *av[] = { "git", "commit", "-q", "-m", "second" };
        CHECK_EQ(RUN_GIT(av, out, sizeof(out)), 0, "git commit second");
    }

    char head2_sha[64] = {0};
    {
        const char *av[] = { "git", "rev-parse", "HEAD" };
        int rc = RUN_GIT(av, out, sizeof(out));
        rstrip(out);
        CHECK_EQ(rc, 0, "git rev-parse HEAD(第二次) 退出码 0");
        CHECK(is_hex40(out) && strcmp(out, head_sha) != 0, "第二次 HEAD 与第一次不同");
        if (is_hex40(out)) strcpy(head2_sha, out);
    }

    {
        const char *av[] = { "git", "push", REMOTE_URL, PUSH_REFSPEC };
        CHECK_EQ(RUN_GIT(av, out, sizeof(out)), 0, "第二次 git push 退出码 0");
    }
    {
        char refbuf[128];
        if (read_file(REMOTE_DIR "/refs/heads/main", refbuf, sizeof(refbuf)) > 0) {
            rstrip(refbuf);
            CHECK_STREQ(refbuf, head2_sha, "远端 refs/heads/main 已前进到第二次 HEAD");
        } else {
            FAIL("第二次 push 后读不到远端 refs/heads/main");
        }
    }
    /* 第二次的对象也必须能从远端解出来 —— 增量 pack 里只有 b.txt 那个 blob
     * 和新的 tree/commit, 缺了 base 就解不开, 所以这条同时验证了
     * "远端保留了第一次的对象"。 */
    {
        const char *av[] = { "git", "--git-dir=" REMOTE_DIR, "cat-file", "-p",
                             "refs/heads/main:b.txt" };
        int rc = RUN_GIT(av, out, sizeof(out));
        CHECK_EQ(rc, 0, "远端 cat-file -p refs/heads/main:b.txt 退出码 0");
        CHECK_STREQ(out, "second push\n", "远端 main:b.txt 内容与写入一致");
    }

    /* ---- 7.5 内核 wait status 编码回归 ----
     *
     * git 的 die() 就是 exit(128)。如果内核拿 `exit_code >= 128` 反推
     * "信号致死"，exit(128) 会被编码成 (128-128)&0x7f = 0，父进程读到
     * WIFEXITED && WEXITSTATUS == 0 —— **一切致命错误都表现为成功退出**。
     * git 移植期正被这条掩盖过：打 fatal 却 rc=0，三个独立问题叠在一起
     * 看起来只有一个。
     *
     * `--git-dir` 指向不存在的仓库必然命中 builtin/config.c:536 的
     * die("not in a git directory")，所以这条就是 die 退出码的直接探针。
     * 故意脏 stderr，测完还原总闸的账本。 */
    {
        int saved_fatal = g_stderr_fatal;
        g_stderr_fatal = 0;

        const char *av[] = { "git", "--git-dir=" ROOT_DIR "/nonexistent",
                             "config", "a.b", "c" };
        CHECK_EQ(RUN_GIT(av, out, sizeof(out)), 128,
                 "die() 退出码 128(不是 0: 内核 wait status 编码回归)");

        g_stderr_fatal = saved_fatal;
    }

    /* ---- 8. 负向: 推一个不存在的 refspec 必须失败 ----
     *
     * 这条**故意**会往 stderr 写 error:, 所以先把总闸的账本存下来, 测完还原,
     * 否则下面那条"全程 stderr 无 fatal"会被自己的负向测试弄红。
     * 它要证明的是: 失败没有被吞掉(fatal 确实出现了), 而不是"命令返回非 0" ——
     * 后者有可能是被别的原因连带弄挂的。 */
    {
        int saved_fatal = g_stderr_fatal;
        g_stderr_fatal = 0;

        const char *av[] = { "git", "push", REMOTE_URL, "refs/heads/nope:refs/heads/nope" };
        int rc = RUN_GIT(av, out, sizeof(out));
        CHECK(rc != 0, "推不存在的 refspec 返回非 0(失败没被吞)");
        CHECK(g_stderr_fatal, "推不存在的 refspec 在 stderr 上报了致命错误");

        g_stderr_fatal = saved_fatal;
    }

    /* ---- 9. 总闸 ---- */
    if (g_stderr_warn_count > 0) {
        printf("[git] 提示: 全程有 %d 次调用在 stderr 上打了 warning(不判红)\n",
               g_stderr_warn_count);
    }
    CHECK(!g_stderr_fatal, "全程 stderr 没有 fatal:/error:(正向用例)");
    if (g_stderr_fatal) {
        printf("[git] 首次致命输出来自 `%s`, 原文:\n%s\n", g_stderr_cmd, g_stderr_first);
    }

    /* 现场留在 /tmp/git_push: 红了可以 `cd /tmp/git_push/local` 手敲,
     * 远端是 /tmp/git_push/remote.git, 下次运行开头会被清掉。 */

    TEST_END();
}
