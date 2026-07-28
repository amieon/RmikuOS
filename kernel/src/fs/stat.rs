#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct Stat {
    pub file_type: u8,
    pub mode: u16,
    pub uid: u32,
    pub gid: u32,
    pub size: usize,
    pub reserved: [u8; 4],
}

pub const STAT_TYPE_FILE: u8 = 1;
pub const STAT_TYPE_DIR: u8 = 2;
pub const STAT_TYPE_CHAR: u8 = 3;
pub const STAT_TYPE_PIPE: u8 = 4;

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
            reserved: [0; 4],
        }
    }
}
