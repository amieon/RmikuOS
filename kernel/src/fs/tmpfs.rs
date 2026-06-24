use alloc::collections::BTreeMap;
use alloc::str;
use alloc::string::String;
use alloc::sync::Arc;
use alloc::vec::Vec;
use crate::fs::dirent::{DirEntry, FILE_TYPE_DIR, FILE_TYPE_FILE};
use crate::fs::mount::FileSystem;
use crate::sync::spin::Mutex;
use super::*;


enum TmpfsNode{
    File(Arc<Mutex<Vec<u8>>>),
    Dir(Arc<Mutex<BTreeMap<String, TmpfsNode>>>)
}

impl TmpfsNode {
    fn clone_ref(&self) -> TmpfsNode {
        match self {
            TmpfsNode::File(d) => TmpfsNode::File(d.clone()), 
            TmpfsNode::Dir(d) => TmpfsNode::Dir(d.clone()),
        }
    }
}

pub struct TmpfsInode {
    node: TmpfsNode,    
}

pub struct TmpfsFs {
    root: Arc<Mutex<BTreeMap<String, TmpfsNode>>>, 
}

impl TmpfsFs {
    pub fn new() -> Self {
        TmpfsFs {
            root: Arc::new(Mutex::new(BTreeMap::new())),  
        }
    }
}

impl crate::fs::mount::FileSystem for TmpfsFs {
    fn root_inode(self: Arc<Self>) -> super::InodeRef {
        Arc::new(TmpfsInode{node : TmpfsNode::Dir(self.root.clone()),})
    } 
}

impl Inode for TmpfsInode {
    fn metadata(&self) -> Metadata {
        match &self.node{
            TmpfsNode::File(data) => 
            Metadata { inode_type: InodeType::File, size:  data.lock().len(),},
            TmpfsNode::Dir(_) => 
            Metadata { inode_type: InodeType::Directory, size:  0,}
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
            let dir = dir.lock();
            let child = dir.get(name)?;        
            Some(Arc::new(TmpfsInode { node: child.clone_ref() }))  
        }
        TmpfsNode::File(_) => None,  
    }
    }

    fn open(&self) -> Option<FileRef> {
        match &self.node {
            TmpfsNode::File(data) => {
                Some(Arc::new(TmpfsFile::new(data.clone())))
            }
            TmpfsNode::Dir(_) => {
                Some(Arc::new(ReadOnlyDirFile::new(self.getdents())))
            }
        }
    }

    fn getdents(&self) -> Vec<DirEntry> {
        let mut entries : Vec<DirEntry> = Vec::new();

        if let TmpfsNode::Dir(dir) = &self.node {
            let dir = dir.lock();
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
                let mut dir = dir.lock();
                if dir.contains_key(name) {
                    return None; 
                }
                // 新建一个空文件节点
                let file_node = TmpfsNode::File(Arc::new(Mutex::new(Vec::new())));
                dir.insert(String::from(name), file_node.clone_ref());
                Some(Arc::new(TmpfsInode { node: file_node }))
            }
            TmpfsNode::File(_) => None,   
        }
    }

    fn mkdir(&self, name: &str) -> Option<InodeRef> {
        match &self.node {
            TmpfsNode::Dir(dir) => {
                let mut dir = dir.lock();
                if dir.contains_key(name) {
                    return None;
                }
                
                let dir_node = TmpfsNode::Dir(Arc::new(Mutex::new(BTreeMap::new())));
                dir.insert(String::from(name), dir_node.clone_ref());
                Some(Arc::new(TmpfsInode { node: dir_node }))
            }
            TmpfsNode::File(_) => None,
        }
    }

    fn unlink(&self, name: &str) -> isize {
        match &self.node {
            TmpfsNode::Dir(dir) => {
                let mut dir = dir.lock(); 
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
                let mut dir = dir.lock(); 
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
                let mut dir = dir.lock(); 
                let is_empty_dir = match dir.get(name) {
                    Some(TmpfsNode::Dir(child)) => {
                        child.lock().is_empty()
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
}

impl File for TmpfsFile {
    fn readable(&self) -> bool { true }
    fn writable(&self) -> bool { true }

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
        Stat::new(STAT_TYPE_FILE, self.data.lock().len())
    }

}


impl TmpfsFile {
    pub fn new(data: Arc<Mutex<Vec<u8>>>) -> Self {
        TmpfsFile {
            data,                         
            offset: Mutex::new(0),
        }
    }
}