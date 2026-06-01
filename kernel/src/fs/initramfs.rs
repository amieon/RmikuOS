use alloc::sync::Arc;
use alloc::vec::Vec;

use crate::sync::spin::Mutex;

use super::dirent::{
    DirEntry,
    DIRENT_SIZE,
    FILE_TYPE_DIR,
    FILE_TYPE_FILE,
};
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

pub struct DirFile {
    entries: Vec<DirEntry>,
    offset: Mutex<usize>,
}

impl DirFile {
    pub fn new(entries: Vec<DirEntry>) -> Self {
        Self {
            entries,
            offset: Mutex::new(0),
        }
    }
}

impl File for DirFile {
    fn readable(&self) -> bool {
        true
    }

    fn writable(&self) -> bool {
        false
    }

    fn is_dir(&self) -> bool {
        true
    }

    fn read(&self, _buf: &mut [u8]) -> isize {
        -1
    }

    fn write(&self, _buf: &[u8]) -> isize {
        -1
    }

    fn getdents(&self, buf: &mut [u8]) -> isize {
        let mut offset = self.offset.lock();

        let max_entries = buf.len() / DIRENT_SIZE;
        if max_entries == 0 {
            return 0;
        }

        let mut written = 0usize;

        while *offset < self.entries.len() && written < max_entries {
            let entry = self.entries[*offset];

            let start = written * DIRENT_SIZE;
            let end = start + DIRENT_SIZE;

            buf[start..end].copy_from_slice(entry.as_bytes());

            *offset += 1;
            written += 1;
        }

        (written * DIRENT_SIZE) as isize
    }
}


static MOTD: &[u8] = b"Welcome to RmikuOS initramfs!\n";

fn strip_numeric_prefix(name: &str) -> &str {
    let bytes = name.as_bytes();

    let mut i = 0usize;
    while i < bytes.len() && bytes[i].is_ascii_digit() {
        i += 1;
    }

    if i > 0 && i < bytes.len() && bytes[i] == b'_' {
        &name[i + 1..]
    } else {
        name
    }
}

pub fn open(path: &str) -> Option<FileRef> {
    let path = path.trim();

    if path == "/" {
        let entries = alloc::vec![
            DirEntry::new("bin", FILE_TYPE_DIR),
            DirEntry::new("etc", FILE_TYPE_DIR),
        ];
        return Some(Arc::new(DirFile::new(entries)));
    }

    if path == "/etc" || path == "/etc/" {
        let entries = alloc::vec![
            DirEntry::new("motd", FILE_TYPE_FILE),
        ];
        return Some(Arc::new(DirFile::new(entries)));
    }

    if path == "/bin" || path == "/bin/" {
        let mut entries = Vec::new();

        for id in 0..crate::loader::num_apps() {
            let app_name = crate::loader::get_app_name(id);
            let short_name = strip_numeric_prefix(app_name);
            entries.push(DirEntry::new(short_name, FILE_TYPE_FILE));
        }

        return Some(Arc::new(DirFile::new(entries)));
    }

    if path == "/etc/motd" || path == "motd" {
        return Some(Arc::new(MemFile::new(MOTD)));
    }

    let name = if let Some(rest) = path.strip_prefix("/bin/") {
        rest
    } else {
        path.rsplit('/').next().unwrap_or(path)
    };

    if let Some(app_id) = crate::loader::find_app(name) {
        let data = crate::loader::get_app_data(app_id);
        return Some(Arc::new(MemFile::new(data)));
    }

    None
}