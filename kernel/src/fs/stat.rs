#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct Stat {
    pub file_type: u8,
    pub mode: u16,
    pub uid: u32,
    pub gid: u32,
    pub size: usize,
    /// 最后修改时间(epoch 秒)。未校准墙钟(未跑 ntpdate)时为 0。
    /// 复用原 reserved[4], 保持 32 字节布局不变。
    pub mtime: u32,
    /// 字符设备的次设备号(仅 STAT_TYPE_CHAR 有意义)。占原 reserved 尾部 4 字节,
    /// 仍是 32 字节。用户态 st_rdev 取它, isatty() 靠它区分"终端"和 /dev/null
    /// —— 两者都是 STAT_TYPE_CHAR, 只看类型分不开。
    pub dev: u32,
}

pub const STAT_TYPE_FILE: u8 = 1;
pub const STAT_TYPE_DIR: u8 = 2;
pub const STAT_TYPE_CHAR: u8 = 3;
pub const STAT_TYPE_PIPE: u8 = 4;
pub const STAT_TYPE_SOCKET: u8 = 5;
pub const STAT_TYPE_SYMLINK: u8 = 6;

/// 字符设备的"次设备号"。值随手按 Linux 惯例取, 只要内核内不重复即可:
/// /dev/null = makedev(1,3), /dev/urandom = makedev(1,9), 控制台 = makedev(5,0)。
pub const DEV_NULL: u32 = 0x0103;
pub const DEV_URANDOM: u32 = 0x0109;
pub const DEV_CONSOLE: u32 = 0x0500;

// ===== 权限位(低 12 位) =====
pub const S_IRWXU: u16 = 0o700;
pub const S_IRUSR: u16 = 0o400;
pub const S_IWUSR: u16 = 0o200;
pub const S_IXUSR: u16 = 0o100;
pub const S_IRWXG: u16 = 0o070;
pub const S_IRGRP: u16 = 0o040;
pub const S_IWGRP: u16 = 0o020;
pub const S_IXGRP: u16 = 0o010;
pub const S_IRWXO: u16 = 0o007;
pub const S_IROTH: u16 = 0o004;
pub const S_IWOTH: u16 = 0o002;
pub const S_IXOTH: u16 = 0o001;
pub const S_ISUID: u16 = 0o4000;
pub const S_ISGID: u16 = 0o2000;
pub const S_ISVTX: u16 = 0o1000;

impl Stat {
    pub const fn new(file_type: u8, size: usize, mode: u16, uid: u32, gid: u32) -> Self {
        Self {
            file_type,
            mode,
            uid,
            gid,
            size,
            mtime: 0,
            dev: 0,
        }
    }

    /// 链式设置修改时间(epoch 秒), 供各文件系统在 stat() 时填墙钟真值。
    pub fn with_mtime(mut self, t: u32) -> Self {
        self.mtime = t;
        self
    }

    /// 链式设置设备号(只有字符设备用得上)。
    pub fn with_dev(mut self, d: u32) -> Self {
        self.dev = d;
        self
    }
}
