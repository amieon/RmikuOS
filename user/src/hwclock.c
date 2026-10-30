/* hwclock.c —— 手工读写 fake-hwclock 的 stamp 文件
 *
 * 平时不需要它：开机内核自动读 stamp（drivers/rtc/stamp.rs），
 * ntpdate 成功后也会自动回写。这个命令是给两种场合用的：
 *
 *   hwclock -w   已经用别的方式校准了墙钟(比如手工 set_wall_clock)，
 *                要把当前值存下来供下次开机用
 *   hwclock -s   stamp 有值但内核没读（比如 /var 是在 stamp 就位之前挂的），
 *                手工把 stamp 灌进内核墙钟
 *   hwclock      不带参数 = 打印当前墙钟和 stamp 内容，用于排查
 */
#include "user.h"

static void show_stamp(void) {
    unsigned long stamp = 0;
    if (hwclock_stamp_read(&stamp) == 0) {
        printf("hwclock: " RTCSTAMP_PATH " = %lu\n", stamp);
    } else {
        printf("hwclock: " RTCSTAMP_PATH " absent or unreadable\n");
    }
}

static void usage(void) {
    fputs("usage: hwclock [-r|-w|-s]\n", stdout);
    fputs("  -r  read stamp (default)\n", stdout);
    fputs("  -w  write current wall clock into stamp\n", stdout);
    fputs("  -s  set kernel wall clock from stamp\n", stdout);
}

int main(int argc, char **argv) {
    const char *opt = "-r";
    if (argc > 1) {
        opt = argv[1];
    }

    if (strcmp(opt, "-r") == 0) {
        time_t t = time(0);
        if (t == 0) {
            printf("hwclock: kernel wall clock not set (epoch 0)\n");
        } else {
            printf("hwclock: kernel wall clock = %ld\n", (long)t);
        }
        show_stamp();
        return 0;
    }

    if (strcmp(opt, "-w") == 0) {
        time_t t = time(0);
        if (t == 0) {
            fputs("hwclock: wall clock not set, nothing to save "
                  "(run `ntpdate` first)\n", stdout);
            return 1;
        }
        if (hwclock_stamp_write((unsigned long)t) != 0) {
            printf("hwclock: write " RTCSTAMP_PATH " failed (errno=%d)\n", errno);
            return 1;
        }
        printf("hwclock: saved %ld to " RTCSTAMP_PATH "\n", (long)t);
        return 0;
    }

    if (strcmp(opt, "-s") == 0) {
        unsigned long stamp = 0;
        if (hwclock_stamp_read(&stamp) != 0) {
            fputs("hwclock: no usable stamp\n", stdout);
            return 1;
        }
        set_wall_clock((usize)stamp * 1000000UL);
        printf("hwclock: kernel wall clock <- %lu\n", stamp);
        return 0;
    }

    usage();
    return 1;
}
