/* 墙钟 / fake-hwclock 测试
 *
 * 内核里"时间从哪来"有三条路（kernel/src/drivers/rtc/ + timer/mod.rs）：
 *
 *   RTC     QEMU virt 的 goldfish(riscv, 纳秒) / LS7A(loongarch, 秒)。
 *           **真机上这段 MMIO 不存在** —— riscv 真板是 I2C 的 PCF8563，
 *           所以 RTC 只当"运气好"的加分项，不能当依赖。
 *   stamp   /var/lib/rtcstamp，Debian fake-hwclock 的同款做法：把"上次离开时
 *           是几点"存成文件，开机由内核读回。这是无 RTC 机型的唯一兜底，
 *           代价是误差 = 停机时长。
 *   ntp     开机后台 ntpdate 校准，写到内核墙钟 + 回写 stamp。
 *
 * 用户态看不到"本次开机到底走了哪条路"（那是 [rtc] 日志），所以这里测**结果**：
 *
 *   1. 开机墙钟要么是 0（全都没成），要么落在 2000~2100 —— 不能是任意垃圾。
 *   2. set_wall_clock 之后 time() 立刻等于设定值，并且和单调钟**同速率**前进。
 *      这条专门抓的一类 bug：WALL_TICKS 打点用错单位（tick 当 us、秒当毫秒），
 *      那样墙钟会比单调钟快/慢整倍数，而单独看 time() 是看不出来的。
 *   3. stamp 的写/读/覆盖/容错。格式必须和内核 stamp.rs 的解析器严格一致，
 *      两边一漂，你只会看到"mtime 还是 1970"，查起来极费劲。
 *   4. 绝对准确度（到底差多少秒）guest 内部没有第二个参考源，只能和宿主
 *      `date` 对表 —— 所以这里只打 [INFO]，不做断言。
 *
 * 副作用说明：本测试会改写墙钟和 /var/lib/rtcstamp，结束时**尽力恢复原值**。
 * 唯一恢复不了的情况：开机时墙钟本来是 0（未校准），而内核没有 unset 接口，
 * 测试结束后墙钟会停在 FIXED_EPOCH。这一点会在输出里明说。
 */
#include "test.h"
#include "time.h"

/* 2026-01-01 00:00:00 UTC。选它只因为好认，且明显区别于 0 和 "现在"。 */
#define FIXED_EPOCH 1767225600UL
#define FIXED_US    (FIXED_EPOCH * 1000000UL)

int main() {
    TEST_START("rtc");

    /* ---------- 1. 开机初始值 ---------- */
    time_t t0 = time(0);
    printf("[INFO] 开机墙钟 = %ld（0 表示 RTC 和 stamp 都没成）\n", (long)t0);
    CHECK(t0 == 0 || (t0 >= 946684800L && t0 < 4102444800L),
          "墙钟要么未校准(0)，要么在 2000-01-01 ~ 2100 之间");

    /* 先存下原值：后面会把 stamp 文件翻来覆去地写，必须能还原 */
    unsigned long saved = 0;
    int saved_ok = (hwclock_stamp_read(&saved) == 0);
    printf("[INFO] 现有 stamp: 有=%d 值=%ld\n", saved_ok, (long)saved);

    /* ---------- 2. 校准以后 time() 就该是设定值 ---------- */
    CHECK_EQ(set_wall_clock((usize)FIXED_US), 0, "set_wall_clock 返回 0");
    time_t t1 = time(0);
    printf("[INFO] 校准后 time() = %ld（设定 %ld）\n", (long)t1, (long)FIXED_EPOCH);
    CHECK(t1 >= (time_t)FIXED_EPOCH && t1 <= (time_t)FIXED_EPOCH + 1,
          "校准后 time() == 设定值（±1s，只可能差在秒边界）");

    /* 三个入口路由同一个值：谁跑偏了这里就会露出来 */
    struct timeval tv;
    struct timespec ts;
    gettimeofday(&tv, 0);
    clock_gettime(CLOCK_REALTIME, &ts);
    CHECK_EQ(tv.tv_sec, (long)t1, "gettimeofday 与 time() 一致");
    CHECK_EQ(ts.tv_sec, (long)t1, "clock_gettime(REALTIME) 与 time() 一致");

    /* ---------- 3. 墙钟与单调钟同速率 ----------
     * 单位陷阱：rk_get_time()（SYS_GET_TIME）返回的是**硬件裸计数器**，
     * riscv 10MHz / loongarch 100MHz，不是秒。拿它当秒写"等 2 个单调秒"，
     * 实际只等了 2 个 tick（几十纳秒），墙钟一步没走 → 这条断言必然 FAIL，
     * 但 FAIL 的是测试不是内核。要比秒就必须用 get_time_us()（微秒）。 */
    isize us0 = get_time_us();
    time_t wall0 = time(0);
    while (get_time_us() - us0 < 2000000) {
        /* 忙等 2 个单调秒：不能用 nanosleep，它本身就走同一个钟 */
    }
    long dus = (long)(get_time_us() - us0);
    long dsec = dus / 1000000L;
    time_t dwall = time(0) - wall0;
    printf("[INFO] 2 秒窗口: 单调走了 %ld us（%ld s），墙钟走了 %ld s\n",
           dus, dsec, (long)dwall);
    CHECK(dsec >= 2, "单调钟确实前进了至少 2 秒");
    CHECK(dwall >= dsec - 1 && dwall <= dsec + 1,
          "墙钟与单调钟同速率（差 ≤1s；单位搞错会整倍数偏离）");

    /* 单调钟不会因为校准而跳变：再校准一次，单调钟不能受影响 */
    isize mono_before = get_time_us();
    set_wall_clock((usize)FIXED_US);
    CHECK_GE(get_time_us(), mono_before,
             "重新校准墙钟不影响单调钟（两者锚点独立）");

    /* ---------- 4. stamp 写读一致 ---------- */
    CHECK_EQ(hwclock_stamp_write(FIXED_EPOCH), 0, "写 stamp 成功");
    unsigned long back = 0;
    CHECK_EQ(hwclock_stamp_read(&back), 0, "读 stamp 成功");
    CHECK_EQ(back, FIXED_EPOCH, "读回来的就是写进去的");

    /* ---------- 5. 覆盖而不是追加 ----------
     * 少了 O_TRUNC 的经典现象：先写 10 位、再写 1 位，文件里剩 "7" + 9 位残渣，
     * 重新解析得到 1767225600 之类的怪数。文件大小是最直接的证据。 */
    CHECK_EQ(hwclock_stamp_write(7), 0, "改写成很短的值");
    back = 0;
    CHECK_EQ(hwclock_stamp_read(&back), 0, "再读一次");
    CHECK_EQ(back, 7, "短值覆盖长值，没有残渣被拼进来");

    struct stat st;
    if (stat(RTCSTAMP_PATH, &st) == 0) {
        printf("[INFO] stamp 文件大小 = %ld\n", (long)st.st_size);
        CHECK_EQ(st.st_size, 2, "只有 2 字节：\"7\" + 换行（证明是 O_TRUNC）");
    } else {
        FAIL("stat /var/lib/rtcstamp 失败");
    }

    /* ---------- 6. 文件内容格式 ---------- */
    int fd = (int)open(RTCSTAMP_PATH, O_RDONLY, 0);
    char raw[64];
    isize rn = (fd >= 0) ? read(fd, raw, sizeof raw - 1) : -1;
    if (fd >= 0) close(fd);
    CHECK(rn > 0, "stamp 文件可读");
    if (rn > 0) {
        raw[rn] = '\0';
        int digits = 0, bad = 0, seen_space = 0;
        for (isize i = 0; i < rn; i++) {
            char c = raw[i];
            if (c >= '0' && c <= '9') {
                if (seen_space) bad = 1;   /* 数字夹在空白之后 = 格式坏了 */
                digits++;
            } else if (c == '\n' || c == '\r' || c == ' ' || c == '\t') {
                seen_space = 1;
            } else {
                bad = 1;
            }
        }
        CHECK_EQ(bad, 0, "内容只有十进制数字 + 尾部空白");
        CHECK_EQ(digits, 1, "写 7 之后确实只有 1 位数字");
    }

    /* ---------- 7. 解析容错（内核 stamp.rs 的规则） ---------- */
    fd = (int)open(RTCSTAMP_PATH, O_WRONLY | O_TRUNC, 0644);
    if (fd >= 0) {
        write(fd, "12345junk\n", 10);
        close(fd);
    }
    back = 0;
    CHECK_EQ(hwclock_stamp_read(&back), 0, "数字后面跟垃圾不算失败");
    CHECK_EQ(back, 12345, "解析停在第一个非数字字符处");

    fd = (int)open(RTCSTAMP_PATH, O_WRONLY | O_TRUNC, 0644);
    if (fd >= 0) {
        write(fd, "garbage", 7);
        close(fd);
    }
    back = 42;
    CHECK_EQ(hwclock_stamp_read(&back), -1, "一个数字都没有 -> 返回 -1");
    CHECK_EQ(back, 42, "失败时不写 *secs（别把 0 塞进去，0 是合法时间戳）");

    /* 空文件同样是失败，不能当成 0 */
    fd = (int)open(RTCSTAMP_PATH, O_WRONLY | O_TRUNC, 0644);
    if (fd >= 0) close(fd);
    back = 42;
    CHECK_EQ(hwclock_stamp_read(&back), -1, "空文件 -> 返回 -1");
    CHECK_EQ(back, 42, "失败时不写 *secs（保留调用方的值）");

    /* ---------- 8. 目录本身在 /var 下 ---------- */
    struct stat sd;
    CHECK(stat("/var/lib", &sd) == 0, "/var/lib 存在（stamp 的落点）");
    /* 注：/var 是 /data 上的 bind 挂载，跨重启保留；如果哪天 bind 没挂上，
     * 这条仍然会成功（只是落在 rootfs 上），所以别指望它能抓挂载。 */

    /* ---------- 9. 文件时间戳也走这口墙钟 ----------
     * 墙钟对不对，最容易被忽略的后果在 stat 上：`ls -l` 全是 1970 就是因为
     * 这条链没接上。此处刚把墙钟校到 FIXED_EPOCH，新建文件的 mtime 必须≈它，
     * 而不是 0、也不是单调秒（那会是个一两千的小数字）。 */
    const char *probe = "/tmp/rtc_probe";
    int pf = (int)open(probe, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (pf >= 0) {
        write(pf, "x", 1);
        close(pf);
    }
    struct stat sp;
    if (stat(probe, &sp) == 0) {
        printf("[INFO] 新文件 mtime = %ld（墙钟 %ld）\n",
               (long)sp.st_mtime, (long)time(0));
        long d = (long)sp.st_mtime - (long)FIXED_EPOCH;
        if (d < 0) d = -d;
        CHECK(d <= 5, "mtime 落在校准后的墙钟上（不是 0、也不是单调秒）");
    } else {
        FAIL("stat 新建文件失败");
    }
    unlink(probe);

    /* ---------- 10. 收尾：恢复 ---------- */
    if (t0 > 0) {
        set_wall_clock((usize)t0 * 1000000UL);
        CHECK(time(0) >= (time_t)t0,
              "墙钟已恢复到测试前的值（最多差 1 秒的秒边界）");
    } else {
        printf("[INFO] 开机时墙钟本来是 0，内核没有 unset 接口，"
               "测试结束后停在 FIXED_EPOCH\n");
    }

    if (saved_ok) {
        hwclock_stamp_write(saved);
        back = 0;
        CHECK(hwclock_stamp_read(&back) == 0 && back == saved,
              "stamp 已恢复为测试前的值");
    } else if (time(0) > 0) {
        /* 原来没有 stamp：留一个现在的时间比删掉更有用（下次开机有底） */
        hwclock_stamp_write((unsigned long)time(0));
    } else {
        unlink(RTCSTAMP_PATH);
    }

    TEST_END();
}
