use alloc::collections::BTreeMap;
use alloc::str;
use alloc::string::String;
use alloc::sync::Arc;
use alloc::vec::Vec;
use crate::fs::dirent::{DirEntry, FILE_TYPE_DIR, FILE_TYPE_FILE};
use crate::fs::mount::FileSystem;
use crate::sync::spin::Mutex;
use super::*;
use super::flag::*;

/// 节点的属主与权限(供 chmod/chown 原地修改)
#[derive(Clone, Copy)]
struct Perms {
    uid: usize,
    gid: usize,
    mode: u16,
}

struct TmpfsFileNode {
    perms: Mutex<Perms>,
    data: Arc<Mutex<Vec<u8>>>,
}

struct TmpfsDirNode {
    perms: Mutex<Perms>,
    children: Arc<Mutex<BTreeMap<String, TmpfsNode>>>,
}

enum TmpfsNode {
    File(Arc<TmpfsFileNode>),
    Dir(Arc<TmpfsDirNode>),
}

impl TmpfsNode {
    fn clone_ref(&self) -> TmpfsNode {
        match self {
            TmpfsNode::File(f) => TmpfsNode::File(f.clone()),
            TmpfsNode::Dir(d) => TmpfsNode::Dir(d.clone()),
        }
    }

    fn perms(&self) -> Perms {
        match self {
            TmpfsNode::File(f) => *f.perms.lock(),
            TmpfsNode::Dir(d) => *d.perms.lock(),
        }
    }
}

pub struct TmpfsInode {
    node: TmpfsNode,    
}

pub struct TmpfsFs {
    root: Arc<TmpfsDirNode>,
}

impl TmpfsFs {
    pub fn new() -> Self {
        TmpfsFs {
            root: Arc::new(TmpfsDirNode {
                perms: Mutex::new(Perms { uid: 0, gid: 0, mode: 0o755 }),
                children: Arc::new(Mutex::new(BTreeMap::new())),
            }),
        }
    }
}

impl crate::fs::mount::FileSystem for TmpfsFs {
    fn root_inode(self: Arc<Self>) -> super::InodeRef {
        Arc::new(TmpfsInode{ node : TmpfsNode::Dir(self.root.clone()),})
    } 
}

impl Inode for TmpfsInode {
    fn metadata(&self) -> Metadata {
        let p = self.node.perms();
        match &self.node {
            TmpfsNode::File(file_node) =>
            Metadata {
                inode_type: InodeType::File,
                size:  file_node.data.lock().len(),
                uid: p.uid,
                gid: p.gid,
                mode: p.mode,
            },
            TmpfsNode::Dir(_) =>
            Metadata {
                inode_type: InodeType::Directory,
                size:  0,
                uid: p.uid,
                gid: p.gid,
                mode: p.mode,
            },
        }
    }

    fn lookup(&self, name: &str) -> Option<InodeRef> {
        if name.is_empty() || name == "." {
            return Some(Arc::new(TmpfsInode {
                node: self.node.clone_ref(),
            }));
        }

        if name == ".." {
            return Some(Arc::new(TmpfsInode {
                node: self.node.clone_ref(),   // 兜底:返回自己
            }));
        }

        match &self.node {
            TmpfsNode::Dir(dir) => {
                let dir = dir.children.lock();
                let child = dir.get(name)?;        
                Some(Arc::new(TmpfsInode { node: child.clone_ref() }))  
            }
            TmpfsNode::File(_) => None,  
        }
    }

    fn open(&self, flags:usize) -> Option<FileRef> {
        match &self.node {
            TmpfsNode::File(file_node) => {
                let p = *file_node.perms.lock();
                Some(Arc::new(TmpfsFile::new(
                    file_node.data.clone(),
                    flags,
                    p.uid as u32,
                    p.gid as u32,
                    p.mode,
                )))
            }
            TmpfsNode::Dir(_) => {
                let p = self.node.perms();
                Some(Arc::new(ReadOnlyDirFile::new_perms(
                    self.getdents(),
                    p.mode,
                    p.uid as u32,
                    p.gid as u32,
                )))
            }
        }
    }

    fn truncate(&self) -> isize {
        match &self.node {
            TmpfsNode::File(file_node) => { file_node.data.lock().clear(); 0 }
            TmpfsNode::Dir(_) => -1,
        }
    }

    fn getdents(&self) -> Vec<DirEntry> {
        let mut entries : Vec<DirEntry> = Vec::new();

        if let TmpfsNode::Dir(dir) = &self.node {
            let dir = dir.children.lock();
            for (name, node) in dir.iter() {
                let typ = match node {
                    TmpfsNode::Dir(_) => FILE_TYPE_DIR,
                    TmpfsNode::File(_) => FILE_TYPE_FILE,  
                };
                entries.push(DirEntry::new(name,typ));
            }
        }

        entries
    }

    fn create(&self, name: &str) -> Option<InodeRef> {
        match &self.node {
            TmpfsNode::Dir(dir) => {
                let mut dir = dir.children.lock();
                if dir.contains_key(name) {
                    return None; 
                }
                // 新建文件: 属主取当前有效 uid/gid, 默认模式 0o644
                let (_u, ue, _g, ge) = crate::task::current_creds();
                let file_node = TmpfsNode::File(Arc::new(TmpfsFileNode {
                    perms: Mutex::new(Perms { uid: ue, gid: ge, mode: 0o644 }),
                    data: Arc::new(Mutex::new(Vec::new())),
                }));
                dir.insert(String::from(name), file_node.clone_ref());
                Some(Arc::new(TmpfsInode { node: file_node }))
            }
            TmpfsNode::File(_) => None,   
        }
    }

    fn mkdir(&self, name: &str) -> Option<InodeRef> {
        match &self.node {
            TmpfsNode::Dir(dir) => {
                let mut dir = dir.children.lock();
                if dir.contains_key(name) {
                    return None;
                }
                let (_u, ue, _g, ge) = crate::task::current_creds();
                let dir_node = TmpfsNode::Dir(Arc::new(TmpfsDirNode {
                    perms: Mutex::new(Perms { uid: ue, gid: ge, mode: 0o755 }),
                    children: Arc::new(Mutex::new(BTreeMap::new())),
                }));
                dir.insert(String::from(name), dir_node.clone_ref());
                Some(Arc::new(TmpfsInode { node: dir_node }))
            }
            TmpfsNode::File(_) => None,
        }
    }

    fn unlink(&self, name: &str) -> isize {
        match &self.node {
            TmpfsNode::Dir(dir) => {
                let mut dir = dir.children.lock(); 
                match dir.get(name) {
                    Some(TmpfsNode::File(_)) => {
                        dir.remove(name);
                        0
                    }
                    Some(TmpfsNode::Dir(_)) => -1,  
                    None => -1,                     
                }
            }
            TmpfsNode::File(_) => -1, 
        }
    }

    fn remove_recursive(&self, name: &str) -> isize {
        match &self.node {
            TmpfsNode::Dir(dir) => {
                let mut dir = dir.children.lock(); 
                match dir.get(name) {
                    Some(_) => {
                        dir.remove(name);
                        0
                    }
                    None => -1,                     
                }
            }
            TmpfsNode::File(_) => -1, 
        }
    }

    fn rmdir(&self, name: &str) -> isize {
        match &self.node {
            TmpfsNode::Dir(dir) => {
                let mut dir = dir.children.lock(); 
                let is_empty_dir = match dir.get(name) {
                    Some(TmpfsNode::Dir(child)) => {
                        child.children.lock().is_empty()
                    }
                    Some(TmpfsNode::File(_)) => false,  
                    None => false,                     
                };
                if is_empty_dir {
                    dir.remove(name);
                    0
                }
                else{
                    -1
                }
            }
            TmpfsNode::File(_) => -1, 
        }
    }

    fn chmod(&self, new_mode: u16) -> isize {
        let new_mode = new_mode & 0o7777;
        match &self.node {
            TmpfsNode::File(f) => {
                let mut p = f.perms.lock();
                p.mode = new_mode;
                0
            }
            TmpfsNode::Dir(d) => {
                let mut p = d.perms.lock();
                p.mode = new_mode;
                0
            }
        }
    }

    fn chown(&self, uid: usize, gid: usize) -> isize {
        // 简化: 仅 euid==0 可调用(由系统调用层约束)。usize::MAX 表示不改该项。
        const NO_CHANGE: usize = usize::MAX;
        match &self.node {
            TmpfsNode::File(f) => {
                let mut p = f.perms.lock();
                if uid != NO_CHANGE { p.uid = uid; }
                if gid != NO_CHANGE { p.gid = gid; }
                0
            }
            TmpfsNode::Dir(d) => {
                let mut p = d.perms.lock();
                if uid != NO_CHANGE { p.uid = uid; }
                if gid != NO_CHANGE { p.gid = gid; }
                0
            }
        }
    }
}




pub fn init() {
    let fs: Arc<dyn FileSystem> = Arc::new(TmpfsFs::new());
    crate::fs::mount::mount("/tmp", fs);
}


pub fn is_available() -> bool {
    crate::fs::mount::resolve_mount("/tmp").is_some()
}

pub struct TmpfsFile {
    data: Arc<Mutex<Vec<u8>>>,
    offset: Mutex<usize>,  
    flags: usize,
    mode: u16,
    uid: u32,
    gid: u32,
}

impl File for TmpfsFile {
    fn readable(&self) -> bool {
        let access = self.flags & O_ACCMODE;     
        access == O_RDONLY || access == O_RDWR
    }

    fn writable(&self) -> bool {
        let access = self.flags& O_ACCMODE;
        access == O_WRONLY || access == O_RDWR
    }

    fn read(&self, buf: &mut [u8]) -> isize {
        let data = self.data.lock();
        let mut off = self.offset.lock();
        let mut n = 0;
        while *off < data.len() && n < buf.len() {
            buf[n] = data[*off];
            *off += 1;
            n += 1;
        }
        n as isize
    }

    fn write(&self, buf: &[u8]) -> isize {
        let mut data = self.data.lock();
        let mut off = self.offset.lock();
        if self.is_append() {
            *off = data.len(); 
        }
        for &b in buf {
            if *off < data.len() {
                data[*off] = b;       
            } else {
                data.push(b);         
            }
            *off += 1;
        }
        buf.len() as isize
    }

    fn stat(&self) -> Stat {
        Stat::new(STAT_TYPE_FILE, self.data.lock().len(), self.mode, self.uid, self.gid)
    }

}

impl TmpfsFile {
    pub fn new(data: Arc<Mutex<Vec<u8>>>,flags : usize, uid: u32, gid: u32, mode: u16) -> Self {
        TmpfsFile {
            data,                         
            offset: Mutex::new(0),
            flags,
            mode,
            uid,
            gid,
        }
    }

    pub fn is_append(&self) -> bool {
        return (self.flags | O_APPEND) != 0;
    }
}
