use alloc::vec::Vec;

const FD_STDOUT: usize = 1;
const FD_STDERR: usize = 2;

/*
 * 防止用户传一个巨大 len 把内核拖死。
 * 后面可以改成分块写。
 */
const MAX_WRITE_LEN: usize = 4096;

pub fn sys_write(fd: usize, user_buf: usize, len: usize) -> isize {
    if fd != FD_STDOUT && fd != FD_STDERR {
        return -1;
    }

    if len > MAX_WRITE_LEN {
        return -1;
    }

    let bytes: Vec<u8> = match crate::task::read_current_user_bytes(user_buf, len) {
        Some(bytes) => bytes,
        None => return -1,
    };

    for b in bytes {
        crate::io::uart::putchar_raw(b);
    }

    len as isize
}

pub fn sys_read(fd: usize, user_buf: usize, len: usize) -> isize {
    if fd != 0 {
        return -1;
    }

    if len == 0 {
        return 0;
    }

    let mut count = 0usize;

    while count < len {
        let ch = crate::io::uart::getchar_raw();

        let buf = [ch];

        if crate::task::write_current_user_bytes(user_buf + count, &buf).is_none() {
            return -1;
        }

        count += 1;

        if ch == b'\n' || ch == b'\r' {
            break;
        }
    }

    count as isize
}