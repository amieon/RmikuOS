pub const EPERM: isize = 1;
pub const ENOENT: isize = 2;
pub const ESRCH: isize = 3;
pub const EINTR: isize = 4;
pub const EIO: isize = 5;
pub const ENXIO: isize = 6;
pub const E2BIG: isize = 7;
pub const ENOEXEC: isize = 8;
pub const EBADF: isize = 9;
pub const ECHILD: isize = 10;
pub const EAGAIN: isize = 11;
pub const ENOMEM: isize = 12;
pub const EACCES: isize = 13;
pub const EFAULT: isize = 14;
pub const EBUSY: isize = 16;
pub const EEXIST: isize = 17;
pub const EXDEV: isize = 18;
pub const ENODEV: isize = 19;
pub const ENOTDIR: isize = 20;
pub const EISDIR: isize = 21;
pub const EINVAL: isize = 22;
pub const ENFILE: isize = 23;
pub const EMFILE: isize = 24;
pub const ENOTTY: isize = 25;
pub const ETXTBSY: isize = 26;
pub const EFBIG: isize = 27;
pub const ENOSPC: isize = 28;
pub const ESPIPE: isize = 29;
pub const EROFS: isize = 30;
pub const EMLINK: isize = 31;
pub const EPIPE: isize = 32;
pub const EDOM: isize = 33;
pub const ERANGE: isize = 34;

// ---- 文件系统补充（35..40）----
pub const ENAMETOOLONG: isize = 36;
pub const ENOSYS: isize = 38;
pub const ENOTEMPTY: isize = 39;
pub const ELOOP: isize = 40;

// ---- 其它 ----
pub const EOVERFLOW: isize = 75;
pub const EILSEQ: isize = 84;

// ---- 网络 / socket（88+）----
pub const ENOTSOCK: isize = 88;
pub const EMSGSIZE: isize = 90;
pub const ENOPROTOOPT: isize = 92;
pub const EPROTONOSUPPORT: isize = 93;
pub const ENOTSUP: isize = 95;
pub const EAFNOSUPPORT: isize = 97;
pub const EADDRINUSE: isize = 98;
pub const EADDRNOTAVAIL: isize = 99;
pub const ENETDOWN: isize = 100;
pub const ENETUNREACH: isize = 101;
pub const ECONNABORTED: isize = 103;
pub const ECONNRESET: isize = 104;
pub const ENOBUFS: isize = 105;
pub const EISCONN: isize = 106;
pub const ENOTCONN: isize = 107;
pub const ETIMEDOUT: isize = 110;
pub const ECONNREFUSED: isize = 111;
pub const EHOSTUNREACH: isize = 113;
pub const EALREADY: isize = 114;
pub const EINPROGRESS: isize = 115;

/// 判断一个返回值是不是错误码（libc 与内核共用同一个边界）。
pub fn is_err(r: isize) -> bool {
    r < 0 && r >= -4095
}
