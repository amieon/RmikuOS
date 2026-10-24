pub const EPERM:   isize = 1;
pub const ENOENT:  isize = 2;
pub const EINTR:   isize = 4;
pub const EIO:     isize = 5;
pub const ENXIO:   isize = 6;
pub const EBADF:   isize = 9;
pub const EAGAIN:  isize = 11;
pub const ENOMEM:  isize = 12;
pub const EACCES:  isize = 13;
pub const EFAULT:  isize = 14;
pub const EEXIST:  isize = 17;
pub const ENOTDIR: isize = 20;
pub const EISDIR:  isize = 21;
pub const EINVAL:  isize = 22;
pub const ENFILE:  isize = 23;
pub const EMFILE:  isize = 24;
pub const ENOSPC:  isize = 28;
pub const ESPIPE:  isize = 29;
pub const EPIPE:   isize = 32;
pub const ERANGE:  isize = 34;
pub const ENOSYS:  isize = 38;
pub const ENAMETOOLONG: isize = 36;
pub const EOVERFLOW: isize = 75;
pub const ENOTSUP: isize = 95;
pub const ETIMEDOUT: isize = 110;
pub const ECONNREFUSED: isize = 111;

/// 判断一个返回值是不是错误码（libc 与内核共用同一个边界）。
pub fn is_err(r: isize) -> bool {
    r < 0 && r >= -4095
}
