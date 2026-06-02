extern crate alloc;

use alloc::vec::Vec;

use crate::sync::spin::Mutex;

use super::dirent::{DirEntry, DIRENT_SIZE};
use super::file::File;
use super::stat::{
    Stat,
    STAT_TYPE_DIR,
    STAT_TYPE_FILE,
};

enum ReadOnlyData {
    Static(&'static [u8]),
    Owned(Vec<u8>),
}

impl ReadOnlyData {
    fn as_slice(&self) -> &[u8] {
        match self {
            Self::Static(data) => data,
            Self::Owned(data) => data.as_slice(),
        }
    }

    fn len(&self) -> usize {
        self.as_slice().len()
    }
}

pub struct ReadOnlyMemFile {
    data: ReadOnlyData,
    offset: Mutex<usize>,
}

impl ReadOnlyMemFile {
    pub fn from_static(data: &'static [u8]) -> Self {
        Self {
            data: ReadOnlyData::Static(data),
            offset: Mutex::new(0),
        }
    }

    pub fn from_vec(data: Vec<u8>) -> Self {
        Self {
            data: ReadOnlyData::Owned(data),
            offset: Mutex::new(0),
        }
    }
}

impl File for ReadOnlyMemFile {
    fn readable(&self) -> bool {
        true
    }

    fn writable(&self) -> bool {
        false
    }

    fn stat(&self) -> Stat {
        Stat::new(STAT_TYPE_FILE, self.data.len())
    }

    fn read(&self, buf: &mut [u8]) -> isize {
        let mut offset = self.offset.lock();
        let data = self.data.as_slice();

        if *offset >= data.len() {
            return 0;
        }

        let read_len = core::cmp::min(buf.len(), data.len() - *offset);

        buf[..read_len].copy_from_slice(
            &data[*offset..*offset + read_len],
        );

        *offset += read_len;

        read_len as isize
    }

    fn write(&self, _buf: &[u8]) -> isize {
        -1
    }
}

pub struct ReadOnlyDirFile {
    entries: Vec<DirEntry>,
    offset: Mutex<usize>,
}

impl ReadOnlyDirFile {
    pub fn new(entries: Vec<DirEntry>) -> Self {
        Self {
            entries,
            offset: Mutex::new(0),
        }
    }
}

impl File for ReadOnlyDirFile {
    fn readable(&self) -> bool {
        true
    }

    fn writable(&self) -> bool {
        false
    }

    fn is_dir(&self) -> bool {
        true
    }

    fn stat(&self) -> Stat {
        Stat::new(STAT_TYPE_DIR, self.entries.len() * DIRENT_SIZE)
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