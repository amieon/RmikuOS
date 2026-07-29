use alloc::sync::Arc;
use alloc::vec::Vec;

use super::dirent::DirEntry;
use super::file::FileRef;

pub type InodeRef = Arc<dyn Inode>;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum InodeType {
    File,
    Directory,
}

#[derive(Clone, Copy, Debug)]
pub struct Metadata {
    pub inode_type: InodeType,
    pub size: usize,
    /// 文件拥有者 uid (0 = root)
    pub uid: usize,
    /// 文件拥有者 gid (0 = root)
    pub gid: usize,
    /// 权限模式: 低 12 位为权限 + setuid/setgid/sticky 位(见 stat.rs 的 S_* 常量)
    pub mode: u16,
}

/// 访问请求标志(复用 owner 权限位的低 3 位: R=4, W=2, X=1)
pub const R_OK: u16 = 4;
pub const W_OK: u16 = 2;
pub const X_OK: u16 = 1;

/// POSIX 风格的权限检查。
/// - euid == 0 视为超级用户, 绕过所有检查。
/// - 否则按 拥有者 / 所属组 / 其他人 三级取对应权限位, 判断是否包含 request。
///   "所属组"匹配条件: egid == file_gid **或** 任一附加组(groups) == file_gid。
/// - groups 为当前进程的附加组切片(groups[..ngroups])。
/// request 为 R_OK/W_OK/X_OK 的按位或。
pub fn check_access(
    mode: u16,
    file_uid: usize,
    file_gid: usize,
    euid: usize,
    egid: usize,
    groups: &[usize],
    request: u16,
) -> bool {
    if euid == 0 {
        return true;
    }
    let perms = if euid == file_uid {
        (mode >> 6) & 7
    } else if egid == file_gid || groups.iter().any(|&g| g == file_gid) {
        (mode >> 3) & 7
    } else {
        mode & 7
    };
    (perms & request) == request
}

pub trait Inode: Send + Sync {
    fn metadata(&self) -> Metadata;

    fn lookup(&self, _name: &str) -> Option<InodeRef> {
        None
    }

    fn open(&self, flags:usize) -> Option<FileRef>;

    fn getdents(&self) -> Vec<DirEntry> {
        Vec::new()
    }

    fn is_dir(&self) -> bool {
        self.metadata().inode_type == InodeType::Directory
    }

    fn is_file(&self) -> bool {
        self.metadata().inode_type == InodeType::File
    }

    fn create(&self, name: &str) -> Option<InodeRef>{
        None
    }
    
    fn mkdir(&self, name: &str) -> Option<InodeRef>{
        None
    }

    fn truncate(&self) -> isize {
        -1 
    }

    fn unlink(&self, name: &str) -> isize{
        -1
    }
    
    fn rmdir(&self, name: &str) -> isize{
        -1
    }

    fn remove_recursive(&self, name: &str) -> isize{
        -1
    }

    /// 修改文件权限位。默认不支持(只读文件系统返回 -1)。
    fn chmod(&self, _mode: u16) -> isize {
        -1
    }

    /// 修改文件属主。默认不支持(只读文件系统返回 -1)。
    /// 简化模型: 仅 euid==0 可改(由系统调用层约束), 此处只负责写入。
    fn chown(&self, _uid: usize, _gid: usize) -> isize {
        -1
    }

    /// 改名/移动: self 为源所在目录, from 为源名, to 为目标的完整绝对路径(可跨目录)。
    /// 默认不支持(只读文件系统返回 -1)。
    fn rename(&self, _from: &str, _to: &str) -> isize {
        -1
    }

    /// 把文件截断为 len 字节(路径版本, 供 truncate() 系统调用使用)。
    /// 默认不支持(返回 -1)。注意与无参 truncate()(供 O_TRUNC 用)区分。
    fn truncate_to(&self, _len: usize) -> isize {
        -1
    }
}