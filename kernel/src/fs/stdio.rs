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
        Stat::new(STAT_TYPE_CHAR, 0, 0o666, 0, 0)
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
             * 永远回不到 trap 出口，SIGTSTP / SIGTERM 这类"外部发来的"信号
             * 就永远送不到前台程序手上（表现为 Ctrl+Z 对 cat / sqlite3 无效）。
             *
             * 一个字节都没读到时返回 -EINTR（POSIX），读到了就返回已读长度 ——
             * 数据优先，别让信号把已读的字节吞掉。 */
            let mut spins = 0u32;
            let mut ch = loop {
                if let Some(c) = crate::io::uart::try_getchar_raw() {
                    break c;
                }
                if crate::task::signal_pending_now() {
                    return if count > 0 {
                        count as isize
                    } else {
                        -crate::syscall::errno::EINTR
                    };
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
            None => 0,
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
        Stat::new(STAT_TYPE_CHAR, 0, 0o666, 0, 0)
    }
}

pub fn stdin() -> FileRef {
    Arc::new(Stdin)
}

pub fn stdout() -> FileRef {
    Arc::new(Stdout)
}