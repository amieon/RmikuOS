use alloc::sync::Arc;

use super::file::{File, FileRef};
use super::stat::*;
pub struct Stdin;
pub struct Stdout;

impl File for Stdin {
    fn readable(&self) -> bool {
        true
    }

    fn writable(&self) -> bool {
        false
    }

    fn stat(&self) -> Stat {
        Stat::new(STAT_TYPE_CHAR, 0, 0o666, 0, 0).with_dev(DEV_CONSOLE)
    }

    fn read(&self, buf: &mut [u8]) -> isize {
        if buf.is_empty() {
            return 0;
        }

        let mut count = 0usize;

        while count < buf.len() {
            /* 阻塞等键，但每轮先看一眼有没有待投递的信号。
             *
             * 为什么不能用 getchar_raw() 一把等下去：那个函数在内核里死等，
             * 永远回不到 trap 出口，"外部发来的"信号就永远送不到前台程序手上。
             *
             * 两类信号两种处理，别混：
             *   - 停止类（SIGTSTP/SIGSTOP，默认动作 = 停）：就地停住，
             *     继续之后回到这个循环接着等键，程序完全无感。
             *     不能返回 EINTR —— `cat` 之类见 -1 就打 "read failed" 退出。
             *   - 其余（SIGINT/SIGTERM/装了 handler 的）：返回 -EINTR 让用户态
             *     回 trap 出口去执行对应动作；一个字节都没读到时才报 EINTR，
             *     读到了就返回已读长度（数据优先，别把已读字节吞掉）。 */
            let mut spins = 0u32;
            let mut ch = loop {
                if let Some(c) = crate::io::uart::try_getchar_raw() {
                    break c;
                }
                /* 没键可读的空档顺便收个包：放在取键之后，这样网卡锁
                 * 再忙也挡不住键盘采样（顺序反了就是这次卡登录的直接原因）。 */
                crate::drivers::net::maybe_poll();
                /* 这两个判断都要抢全局调度锁，别每轮都做：每 64 轮一次，
                 * 按键响应延迟仍是微秒级。 */
                if spins & 0x3f == 0 {
                    if let Some(stop_sig) = crate::task::take_pending_stop_signal() {
                        crate::task::stop_current_process(stop_sig);
                        continue;   /* 被 SIGCONT 唤醒后回到这里继续等键 */
                    }
                    if crate::task::signal_pending_now() {
                        return if count > 0 {
                            count as isize
                        } else {
                            -crate::syscall::errno::EINTR
                        };
                    }
                }
                spins = spins.wrapping_add(1);
                if spins & 0xfff == 0 {
                    /* 别把 CPU 空转烧掉：让出一次，其他线程还能跑。
                     * 每几千轮一次，切换开销可以忽略。 */
                    crate::task::suspend_current_and_run_next();
                }
            };

            /* Ctrl+C(0x03): 终端驱动识别, 向前台进程投 SIGINT 并立即中断本次
             * read —— syscall 返回用户态后 do_signal 生效:
             * 前台若未忽略(SIG_IGN)则默认终止; shell 忽略则 read 返回已读部分。 */
            if ch == 0x03 {
                crate::task::kill_front_sigint();
                return count as isize;
            }

            /* Ctrl+Z(0x1A): 停住前台进程（SIGTSTP）。终端只负责"按下暂停"，
             * 续跑要用 shell 的 fg/bg —— 内核不知道哪个作业该回到前台。 */
            if ch == 0x1a {
                crate::task::kill_front_tstp();
                return count as isize;
            }

            crate::io::uart::echo_input_char(ch);
            /* ICRNL: 终端回车(CR)统一转成换行(NL)。
             * fgets/local_getline 等只认 \n 结尾, 原始 \r 会让他们一直等。 */
            if ch == b'\r' {
                ch = b'\n';
            }
            buf[count] = ch;
            count += 1;

            if ch == b'\n' {
                break;
            }
        }

        count as isize
    }

    fn read_nonblock(&self, buf: &mut [u8]) -> isize {
        if buf.is_empty() { return 0; }
        match crate::io::uart::try_getchar_raw() {
            Some(ch) => {
                if ch == 0x03 {
                    crate::task::kill_front_sigint();
                    return 0;
                }
                if ch == 0x1a {
                    crate::task::kill_front_tstp();
                    return 0;
                }
                crate::io::uart::echo_input_char(ch);
                buf[0] = if ch == b'\r' { b'\n' } else { ch };
                1
            }
            None => {
                /* 只在取键落空时收包，理由同阻塞读：别让网卡锁挡住键盘 */
                crate::drivers::net::maybe_poll();
                0
            }
        }
    }

    fn write(&self, _buf: &[u8]) -> isize {
        -1
    }
}

impl File for Stdout {
    fn readable(&self) -> bool {
        false
    }

    fn writable(&self) -> bool {
        true
    }

    fn read(&self, _buf: &mut [u8]) -> isize {
        -1
    }

    fn write(&self, buf: &[u8]) -> isize {
        for &ch in buf {
            crate::io::uart::putchar_raw(ch);
        }

        buf.len() as isize
    }
    fn stat(&self) -> Stat {
        Stat::new(STAT_TYPE_CHAR, 0, 0o666, 0, 0).with_dev(DEV_CONSOLE)
    }
}

pub fn stdin() -> FileRef {
    Arc::new(Stdin)
}

pub fn stdout() -> FileRef {
    Arc::new(Stdout)
}