#[repr(C)]
#[derive(Clone, Copy)]
pub struct DirEntry {
    pub file_type: u8,
    pub name_len: u8,
    pub reserved: [u8; 6],
    pub name: [u8; 56],
}

pub const DIRENT_SIZE: usize = core::mem::size_of::<DirEntry>();

pub const FILE_TYPE_FILE: u8 = 1;
pub const FILE_TYPE_DIR: u8 = 2;

impl DirEntry {
    pub fn new(name: &str, file_type: u8) -> Self {
        let mut entry = Self {
            file_type,
            name_len: 0,
            reserved: [0; 6],
            name: [0; 56],
        };

        let bytes = name.as_bytes();
        let len = core::cmp::min(bytes.len(), entry.name.len());

        entry.name[..len].copy_from_slice(&bytes[..len]);
        entry.name_len = len as u8;

        entry
    }

    pub fn as_bytes(&self) -> &[u8] {
        unsafe {
            core::slice::from_raw_parts(
                self as *const Self as *const u8,
                DIRENT_SIZE,
            )
        }
    }
}