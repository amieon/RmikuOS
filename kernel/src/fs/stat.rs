#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct Stat {
    pub file_type: u8,
    pub reserved: [u8; 7],
    pub size: usize,
}

pub const STAT_TYPE_FILE: u8 = 1;
pub const STAT_TYPE_DIR: u8 = 2;
pub const STAT_TYPE_CHAR: u8 = 3;

impl Stat {
    pub const fn new(file_type: u8, size: usize) -> Self {
        Self {
            file_type,
            reserved: [0; 7],
            size,
        }
    }
}