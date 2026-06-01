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
use super::inode::{
    Inode,
    InodeRef,
    InodeType,
    Metadata,
};

static MOTD: &[u8] = b"Welcome to RmikuOS initramfs!\n";

#[derive(Clone, Copy)]
enum InitramfsNode {
    Root,
    Bin,
    Etc,
    Motd,
    App(usize),
}

pub struct InitramfsInode {
    node: InitramfsNode,
}

impl InitramfsInode {
    fn new(node: InitramfsNode) -> Self {
        Self { node }
    }

    fn root() -> Self {
        Self::new(InitramfsNode::Root)
    }
}

pub fn root_inode() -> InodeRef {
    Arc::new(InitramfsInode::root())
}

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

impl Inode for InitramfsInode {
    fn metadata(&self) -> Metadata {
        match self.node {
            InitramfsNode::Root |
            InitramfsNode::Bin |
            InitramfsNode::Etc => Metadata {
                inode_type: InodeType::Directory,
                size: 0,
            },
            InitramfsNode::Motd => Metadata {
                inode_type: InodeType::File,
                size: MOTD.len(),
            },
            InitramfsNode::App(id) => Metadata {
                inode_type: InodeType::File,
                size: crate::loader::get_app_data(id).len(),
            },
        }
    }

    fn lookup(&self, name: &str) -> Option<InodeRef> {
        match self.node {
            InitramfsNode::Root => match name {
                "" | "." => Some(root_inode()),
                "bin" => Some(Arc::new(InitramfsInode::new(InitramfsNode::Bin))),
                "etc" => Some(Arc::new(InitramfsInode::new(InitramfsNode::Etc))),
                _ => None,
            },

            InitramfsNode::Bin => {
                if name == "." {
                    return Some(Arc::new(InitramfsInode::new(InitramfsNode::Bin)));
                }

                let app_id = crate::loader::find_app(name)?;
                Some(Arc::new(InitramfsInode::new(InitramfsNode::App(app_id))))
            }

            InitramfsNode::Etc => match name {
                "." => Some(Arc::new(InitramfsInode::new(InitramfsNode::Etc))),
                "motd" => Some(Arc::new(InitramfsInode::new(InitramfsNode::Motd))),
                _ => None,
            },

            InitramfsNode::Motd | InitramfsNode::App(_) => None,
        }
    }

    fn open(&self) -> Option<FileRef> {
        match self.node {
            InitramfsNode::Root |
            InitramfsNode::Bin |
            InitramfsNode::Etc => {
                Some(Arc::new(DirFile::new(self.getdents())))
            }

            InitramfsNode::Motd => {
                Some(Arc::new(MemFile::new(MOTD)))
            }

            InitramfsNode::App(id) => {
                Some(Arc::new(MemFile::new(crate::loader::get_app_data(id))))
            }
        }
    }

    fn getdents(&self) -> Vec<DirEntry> {
        match self.node {
            InitramfsNode::Root => {
                let mut entries = Vec::new();
                entries.push(DirEntry::new("bin", FILE_TYPE_DIR));
                entries.push(DirEntry::new("etc", FILE_TYPE_DIR));
                entries
            }

            InitramfsNode::Bin => {
                let mut entries = Vec::new();

                for id in 0..crate::loader::num_apps() {
                    let app_name = crate::loader::get_app_name(id);
                    let short_name = strip_numeric_prefix(app_name);
                    entries.push(DirEntry::new(short_name, FILE_TYPE_FILE));
                }

                entries
            }

            InitramfsNode::Etc => {
                let mut entries = Vec::new();
                entries.push(DirEntry::new("motd", FILE_TYPE_FILE));
                entries
            }

            InitramfsNode::Motd | InitramfsNode::App(_) => Vec::new(),
        }
    }
}

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

        buf[..read_len].copy_from_slice(
            &self.data[*offset..*offset + read_len]
        );

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