//! `getrandom(2)` —— 熵的统一出口。
//!
//! 语义刻意对齐 Linux，只在一处更严格：
//!
//!   Linux 在熵池未初始化时阻塞（除非 GRND_NONBLOCK）；
//!   本内核没有"等待熵池"这个状态，设备不在就直接返回 `-EAGAIN`，
//!   两种 flags 都一样。宁可让调用方看到失败，也不要给它一个假的成功。

use crate::syscall::errno::{EAGAIN, EFAULT, EINVAL};

/// 已知标志位都接受，语义为空：本内核只有一个熵源，不区分
/// /dev/random 与 /dev/urandom，也从不返回"弱随机"。
const GRND_KNOWN: usize = 0x0001 | 0x0002 | 0x0004; // NONBLOCK | RANDOM | INSECURE

pub fn sys_getrandom(buf: usize, len: usize, flags: usize) -> isize {
    if flags & !GRND_KNOWN != 0 {
        return -EINVAL;
    }
    if len == 0 {
        return 0;
    }
    if buf == 0 {
        return -EFAULT;
    }
    if !crate::drivers::rng::available() {
        return -EAGAIN;
    }

    // 分块：设备一次一页，但没必要为几十字节去分配；栈上 256 字节够用，
    // 也避免把"用户缓冲区是否连续"这种判断带进内核。
    let mut chunk = [0u8; 256];
    let mut done = 0usize;

    while done < len {
        let n = core::cmp::min(len - done, chunk.len());

        if !crate::drivers::rng::fill_bytes(&mut chunk[..n]) {
            // 已经交出去的那部分不吞掉：返回已完成字节数，符合 Linux 行为
            return if done > 0 { done as isize } else { -EAGAIN };
        }

        if crate::task::write_current_user_bytes(buf + done, &chunk[..n]).is_none() {
            return if done > 0 { done as isize } else { -EFAULT };
        }

        done += n;
    }

    done as isize
}
