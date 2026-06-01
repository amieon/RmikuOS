use alloc::sync::Arc;

use crate::sync::spin::Mutex;

use super::file::{File, FileRef};

pub struct MemFile {
    data: &'static [u8],
    offset: Mutex<usize>,
}

impl MemFile {
    pub fn new(data: &'static [u8]) -> Self {
        Self {
            data,
            offset: Mutex::new(0),
        }
    }
}

impl File for MemFile {
    fn readable(&self) -> bool {
        true
    }

    fn writable(&self) -> bool {
        false
    }

    fn read(&self, buf: &mut [u8]) -> isize {
        let mut offset = self.offset.lock();

        if *offset >= self.data.len() {
            return 0;
        }

        let available = self.data.len() - *offset;
        let read_len = core::cmp::min(buf.len(), available);

        buf[..read_len].copy_from_slice(&self.data[*offset..*offset + read_len]);

        *offset += read_len;

        read_len as isize
    }

    fn write(&self, _buf: &[u8]) -> isize {
        -1
    }
}

static MOTD: &[u8] = b"Welcome to RmikuOS initramfs!\n";

fn basename(path: &str) -> &str {
    match path.rsplit('/').next() {
        Some(name) => name,
        None => path,
    }
}

pub fn open(path: &str) -> Option<FileRef> {
    let path = path.trim();

    if path == "/etc/motd" || path == "motd" {
        return Some(Arc::new(MemFile::new(MOTD)));
    }

    /*
     * /bin/hello -> loader::find_app("hello")
     * hello      -> loader::find_app("hello")
     */
    let name = if let Some(rest) = path.strip_prefix("/bin/") {
        rest
    } else {
        basename(path)
    };

    if let Some(app_id) = crate::loader::find_app(name) {
        let data = crate::loader::get_app_data(app_id);
        return Some(Arc::new(MemFile::new(data)));
    }

    None
}