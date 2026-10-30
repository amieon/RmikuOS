extern crate alloc;

pub mod dirent;
pub mod file;
pub mod inode;
pub mod path;
pub mod common_file;
pub mod ext4fs;
pub mod tmpfs;
pub mod pipe;
pub mod mount;
pub mod fatfs;
pub mod flag;
pub mod rsext4fs;
pub mod null;
pub mod urandom;

//pub mod initramfs;
mod stdio;

pub use file::{File, FileRef, POLLERR, POLLHUP, POLLIN, POLLNVAL, POLLOUT};
pub use inode::{Inode, InodeRef, Metadata, InodeType};
use crate::fs::inode::{check_access, R_OK, W_OK, X_OK};
use crate::syscall::errno;
pub use stdio::{stdin, stdout};
pub use flag::*;
pub const EOF : isize = 0;

pub const F_DUPFD: usize = 0;
pub const F_GETFD: usize = 1;
pub const F_SETFD: usize = 2;
pub const F_GETFL: usize = 3;
pub const F_SETFL: usize = 4;
/// Linux 的 F_DUPFD_CLOEXEC（1030）。值照抄，方便对拍。
pub const F_DUPFD_CLOEXEC: usize = 1030;

/* 描述符标志（和"文件状态标志"是两套东西）：
 * F_GETFD/F_SETFD 读写的是一个 bit —— FD_CLOEXEC，存在 PCB.fd_cloexec 里。
 * 不能塞进 fd_flags：FD_CLOEXEC=1，而 O_WRONLY 也是 1，会撞车。 */
pub const FD_CLOEXEC: usize = 1;

/// open 时"打开即 cloexec"（Linux 的 02000000）。省掉 open+fcntl 两步的竞态。
pub const O_CLOEXEC: usize = 0o2000000;
pub const O_NONBLOCK: usize = 2048;

/// `F_GETFL` 报出来的位：访问模式 + 文件状态标志。
pub const F_GETFL_MASK: usize = O_ACCMODE | O_APPEND | O_NONBLOCK;
/// `F_SETFL` 只能改这两个 —— 访问模式在 open 那一刻定了，POSIX 不允许改。
/// 少掩这一下，`fcntl(fd, F_SETFL, O_NONBLOCK)` 会把 O_RDWR 悄悄改成 O_RDONLY。
pub const F_SETFL_MASK: usize = O_APPEND | O_NONBLOCK;



pub fn normalize_path(cwd: &str, path: &str) -> Option<alloc::string::String> {
    path::normalize_path(cwd, path)
}

pub fn lookup(path: &str) -> Option<InodeRef> {
    path::lookup_abs_path(path)
}

pub fn lookup_at(cwd: &str, path: &str) -> Option<InodeRef> {
    path::lookup_path_at(cwd, path)
}

pub fn lookup_no_follow(path: &str) -> Option<InodeRef> {
    path::lookup_abs_path_no_follow(path)
}

pub fn lookup_no_follow_at(cwd: &str, path: &str) -> Option<InodeRef> {
    path::lookup_path_at_no_follow(cwd, path)
}

/// 新建文件时该落什么权限位：mode 先按 umask 收窄（POSIX），再应用到 inode。
pub fn mode_with_umask(mode: usize) -> u16 {
    ((mode & 0o777) & !(crate::task::current_umask() as usize)) as u16
}

/// 打开文件。失败返回 errno（正数），由 syscall 层取负。
///
/// 以前这里返回 `Option`，syscall 层一律写 `-1` —— 而 `-1` 就是 `-EPERM`，
/// 于是"文件不存在"被用户态看成 "Operation not permitted"。git 找 `/dev/null`
/// 结果报 EPERM 就是被这个吞掉的。
pub fn open_at(cwd: &str, path: &str, flags: usize, mode: usize) -> Result<FileRef, isize> {
    // 绝对路径只算一次：建文件与"告诉目录 fd 自己是谁"都要用
    let abs = path::normalize_path(cwd, path).ok_or(errno::EINVAL)?;

    /* 内置设备节点（/dev/null）绕开挂载表：它不是任何文件系统里的 inode。
     * 放这么前面是因为它必须在 O_CREAT 建文件的逻辑之前拦下 —— 否则
     * `open("/dev/null", O_CREAT)` 会先创建一个普通文件把设备遮住。
     * O_CREAT|O_EXCL 对已存在的设备照 POSIX 报 EEXIST。 */
    if let Some(dev) = null::builtin_device(&abs) {
        if flags & O_CREAT != 0 && flags & O_EXCL != 0 {
            return Err(errno::EEXIST);
        }
        return Ok(dev);
    }

    let inode = match path::lookup_path_at(cwd, path) {
        Some(i) => {
            if flags & O_CREAT != 0 && flags & O_EXCL != 0 {
                return Err(errno::EEXIST);
            }
            i
        }
        None => {
            // 文件不存在
            if flags & O_CREAT == 0 {
                return Err(errno::ENOENT);
            }
            // 创建后按 mode（经 umask 收窄）设权限位
            let inode = create_file(&abs)?;
            inode.chmod(mode_with_umask(mode));
            inode
        }
    };

    // 访问检查(现有文件): 目录/普通文件按 R/W 请求检查; O_EXEC 由 exec 自管
    if !check_open(&inode, flags) {
        // 写打开目录是 EISDIR 而不是 EACCES —— git/mv 靠这个区分"没权限"和"搞错对象"
        let is_dir = inode.metadata().inode_type == InodeType::Directory;
        return Err(if is_dir && (flags & O_ACCMODE) != O_RDONLY {
            errno::EISDIR
        } else {
            errno::EACCES
        });
    }

    if flags & O_TRUNC != 0 {
        inode.truncate();
    }
    inode.open(flags, &abs).ok_or(errno::EACCES)
}

pub fn open(path: &str, flags: usize, mode: usize) -> Result<FileRef, isize> {
    let abs = path::normalize_path("/", path).ok_or(errno::EINVAL)?;

    // 同 open_at：内置设备先于文件系统解析（见那里的注释）。
    if let Some(dev) = null::builtin_device(&abs) {
        if flags & O_CREAT != 0 && flags & O_EXCL != 0 {
            return Err(errno::EEXIST);
        }
        return Ok(dev);
    }

    let inode = match path::lookup_abs_path(&abs) {
        Some(i) => {
            if flags & O_CREAT != 0 && flags & O_EXCL != 0 {
                return Err(errno::EEXIST);
            }
            i
        }
        None => {
            if flags & O_CREAT == 0 {
                return Err(errno::ENOENT);
            }
            let inode = create_file(&abs)?;
            inode.chmod(mode_with_umask(mode));
            inode
        }
    };

    if !check_open(&inode, flags) {
        let is_dir = inode.metadata().inode_type == InodeType::Directory;
        return Err(if is_dir && (flags & O_ACCMODE) != O_RDONLY {
            errno::EISDIR
        } else {
            errno::EACCES
        });
    }

    if flags & O_TRUNC != 0 {
        inode.truncate();
    }
    inode.open(flags, &abs).ok_or(errno::EACCES)
}

pub mod stat;

pub use stat::{
    Stat,
    STAT_TYPE_FILE,
    STAT_TYPE_DIR,
    STAT_TYPE_CHAR,
    STAT_TYPE_SYMLINK,
};
pub use common_file::{
    ReadOnlyDirFile,
    ReadOnlyMemFile,
};


fn stat_of_inode(inode: &InodeRef) -> Stat {
    let meta = inode.metadata();
    let file_type = match meta.inode_type {
        InodeType::File => STAT_TYPE_FILE,
        InodeType::Directory => STAT_TYPE_DIR,
        InodeType::Symlink => STAT_TYPE_SYMLINK,
    };

    Stat::new(file_type, meta.size, meta.mode, meta.uid as u32, meta.gid as u32)
        .with_mtime(crate::timer::now_secs() as u32)
}

pub fn stat_at(cwd: &str, path: &str) -> Option<Stat> {
    let inode = path::lookup_path_at(cwd, path)?;
    Some(stat_of_inode(&inode))
}

pub fn lstat_at(cwd: &str, path: &str) -> Option<Stat> {
    let inode = path::lookup_path_at_no_follow(cwd, path)?;
    Some(stat_of_inode(&inode))
}

pub fn stat(path: &str) -> Option<Stat> {
    let inode = path::lookup_abs_path(path)?;
    Some(stat_of_inode(&inode))
}

pub fn lstat(path: &str) -> Option<Stat> {
    let inode = path::lookup_abs_path_no_follow(path)?;
    Some(stat_of_inode(&inode))
}




use alloc::{format, string::{String, ToString}, vec::Vec};

use crate::{fs::flag::O_RDONLY, io::uart::putchar_raw};

pub fn read_all(path: &str) -> Option<Vec<u8>> {
    let inode = crate::fs::path::lookup_abs_path(path)?;

    let meta = inode.metadata();

    if meta.inode_type != crate::fs::InodeType::File {
        return None;
    }

    let file = inode.open(O_RDONLY, path)?;

    let mut data = Vec::new();
    let mut buf = [0u8; 512];


    loop {

        let n = file.read(&mut buf);

        if n < 0 {
            return None;
        }

        if n == 0 {
            break;
        }


        data.extend_from_slice(&buf[..n as usize]);
    }


    Some(data)
}

// "/tmp/x" → ("/tmp", "x")
// "/tmp"   → ("/", "tmp")
fn split_parent(path: &str) -> (String, String) {
    let trimmed = path.trim_end_matches('/');
    match trimmed.rfind('/') {
        Some(0) => (String::from("/"), String::from(&trimmed[1..])),
        Some(pos) => (String::from(&trimmed[..pos]), String::from(&trimmed[pos+1..])),
        None => (String::from("/"), String::from(trimmed)),
    }
}

pub fn create_file(path: &str) -> Result<InodeRef, isize> {
    let abs = normalize_path("/", path).ok_or(errno::EINVAL)?;
    let (parent, name) = split_parent(&abs);
    let parent_inode = path::lookup_abs_path(&parent).ok_or(errno::ENOENT)?;
    // 在目录中创建文件需要父目录的 写+搜索 权限(root 绕过)
    if !check_dir_write(&parent_inode) {
        return Err(errno::EACCES);
    }
    // 底层 create 返回 None 基本只有一种情况: 挂载的是只读 FS(rootfs 的 ext4-view)。
    // 报 EROFS 而不是 EACCES —— git/mv 靠这个区分"换个地方写"和"没权限".
    parent_inode.create(&name).ok_or(errno::EROFS)
}

pub fn make_dir(path: &str) -> Result<InodeRef, isize> {
    let abs = normalize_path("/", path).ok_or(errno::EINVAL)?;
    let (parent, name) = split_parent(&abs);
    let parent_inode = path::lookup_abs_path(&parent).ok_or(errno::ENOENT)?;

    /* 目标已存在 → EEXIST。必须在权限检查之前：这一步只要遍历，不需要写权限，
     * 而 POSIX 规定 mkdir 对已存在路径一律回 EEXIST（不管它是文件还是目录） */
    if path::lookup_abs_path(&abs).is_some() {
        return Err(errno::EEXIST);
    }

    if !check_dir_write(&parent_inode) {
        return Err(errno::EACCES);
    }
    // 底层 mkdir 返回 None 基本只剩只读 FS（rootfs 的 ext4-view），
    // 与 create_file 一致报 EROFS，让调用方能区分“换个地方写”和“没权限”。
    parent_inode.mkdir(&name).ok_or(errno::EROFS)
}

/// symlink(target, linkpath): 创建内容为 target 的符号链接。
/// target 按字符串原样保存，不要求存在；相对 target 在使用时按链接所在目录解析。
pub fn symlink_file(target: &str, path: &str) -> Option<InodeRef> {
    if target.is_empty() {
        return None;
    }

    let abs = normalize_path("/", path)?;
    let (parent, name) = split_parent(&abs);
    let parent_inode = path::lookup_abs_path(&parent)?;
    if !check_dir_write(&parent_inode) {
        return None;
    }
    parent_inode.symlink(&name, target)
}

/// readlink(path): 读取链接本身保存的目标，不跟随最后一个 symlink。
pub fn readlink_path(path: &str) -> Option<alloc::string::String> {
    let inode = path::lookup_abs_path_no_follow(path)?;
    inode.readlink()
}

/// link(oldpath, newpath): 为现有 inode 增加一个硬链接目录项。
/// 源路径不跟随最后一个 symlink（与 Linux link() 一致），并且不允许链接目录。
pub fn link_file(old: &str, new: &str) -> isize {
    let old_abs = match normalize_path("/", old) {
        Some(path) => path,
        None => return -1,
    };
    let new_abs = match normalize_path("/", new) {
        Some(path) => path,
        None => return -1,
    };

    let old_mount = match mount::mount_point_of(&old_abs) {
        Some(mount) => mount,
        None => return -1,
    };
    let new_mount = match mount::mount_point_of(&new_abs) {
        Some(mount) => mount,
        None => return -1,
    };
    if old_mount != new_mount {
        return -1; // EXDEV
    }

    let target = match path::lookup_abs_path_no_follow(&old_abs) {
        Some(inode) => inode,
        None => return -1,
    };
    if target.is_dir() {
        return -1;
    }

    let (parent, name) = split_parent(&new_abs);
    let parent_inode = match path::lookup_abs_path(&parent) {
        Some(inode) => inode,
        None => return -1,
    };
    if !check_dir_write(&parent_inode) {
        return -1;
    }

    parent_inode.hard_link(&name, &target)
}

pub fn unlink_file(path: &str) -> Option<isize>{
    let abs = normalize_path("/", path)?;
    let (parent, name) = split_parent(&abs);
    let parent_inode = path::lookup_abs_path(&parent)?;
    // 删除需要父目录的 写+搜索 权限
    if !check_dir_write(&parent_inode) {
        return None;
    }
    Some(parent_inode.unlink(&name))                  
}

pub fn remove_dir(path: &str) -> Option<isize> {
    let abs = normalize_path("/", path)?;
    let (parent, name) = split_parent(&abs);
    let parent_inode = path::lookup_abs_path(&parent)?;
    if !check_dir_write(&parent_inode) {
        return None;
    }
    Some(parent_inode.rmdir(&name))
}

pub fn remove_recursive(path: &str) -> Option<isize> {
    let abs = normalize_path("/", path)?;
    let (parent, name) = split_parent(&abs);
    let parent_inode = path::lookup_abs_path(&parent)?;
    if !check_dir_write(&parent_inode) {
        return None;
    }
    Some(parent_inode.remove_recursive(&name))
}

/// 打开现有文件时的访问检查: 按 flags 计算 R/W 请求, 与文件 mode + 当前凭证比对。
/// O_EXEC 由调用方(exec)自行检查 X 权限, 这里放行。
fn check_open(inode: &InodeRef, flags: usize) -> bool {
    if flags & O_EXEC != 0 {
        return true;
    }
    let meta = inode.metadata();
    let (_u, euid, _g, egid) = crate::task::current_creds();
    let (groups, ngroups) = crate::task::current_groups();
    let request = if (flags & O_ACCMODE) == O_WRONLY || (flags & O_ACCMODE) == O_RDWR {
        W_OK
    } else {
        R_OK
    };
    check_access(meta.mode, meta.uid, meta.gid, euid, egid, &groups[..ngroups], request)
}

/// 在目录中创建/删除/改名的必要条件: 父目录需 写+搜索 权限(root 绕过)。
fn check_dir_write(inode: &InodeRef) -> bool {
    let meta = inode.metadata();
    let (_u, euid, _g, egid) = crate::task::current_creds();
    let (groups, ngroups) = crate::task::current_groups();
    check_access(meta.mode, meta.uid, meta.gid, euid, egid, &groups[..ngroups], W_OK | X_OK)
}

/// chmod(path, mode): 修改文件权限位。特权检查由系统调用层(sys_chmod)完成。
pub fn chmod_path(path: &str, mode: u16) -> isize {
    let abs = match normalize_path("/", path) {
        Some(p) => p,
        None => return -1,
    };
    match path::lookup_abs_path(&abs) {
        Some(inode) => inode.chmod(mode),
        None => -1,
    }
}

pub fn access_path(path: &str, request: u16) -> isize {
    let (ruid, _euid, rgid, _egid) = crate::task::current_creds();
    access_path_as(path, request, ruid, rgid)
}

pub fn access_path_as(path: &str, request: u16, uid: usize, gid: usize) -> isize {
    let abs = match normalize_path("/", path) {
        Some(p) => p,
        None => return -errno::EINVAL,
    };
    let inode = match path::lookup_abs_path(&abs) {
        Some(i) => i,
        // 不存在，或路径上有组件不可穿越
        None => return -errno::ENOENT,
    };
    if request == 0 {
        return 0;                    // F_OK：能解析到 inode 即视为存在
    }
    let meta = inode.metadata();
    let (groups, ngroups) = crate::task::current_groups();
    if check_access(meta.mode, meta.uid, meta.gid, uid, gid, &groups[..ngroups], request) {
        0
    } else {
        -errno::EACCES
    }
}

/// chown(path, uid, gid): 修改文件属主。usize::MAX 表示不修改该项。特权检查由系统调用层完成。
pub fn chown_path(path: &str, uid: usize, gid: usize) -> isize {
    let abs = match normalize_path("/", path) {
        Some(p) => p,
        None => return -1,
    };
    match path::lookup_abs_path(&abs) {
        Some(inode) => inode.chown(uid, gid),
        None => -1,
    }
}

/// truncate(path, len): 把路径指定的文件截断为 len 字节。特权检查由系统调用层完成。
pub fn truncate_path(path: &str, len: usize) -> isize {
    let abs = match normalize_path("/", path) {
        Some(p) => p,
        None => return -1,
    };
    match path::lookup_abs_path(&abs) {
        Some(inode) => inode.truncate_to(len),
        None => -1,
    }
}

/// rename(old, new): 移动/改名。同一文件系统内支持跨目录;跨设备返回 -1(EXDEV)。
/// 防自环: 不允许把目录移进它自己的子目录。特权检查由系统调用层完成。
pub fn rename(old: &str, new: &str) -> Result<(), isize> {
    let old_abs = normalize_path("/", old).ok_or(errno::EINVAL)?;
    let new_abs = normalize_path("/", new).ok_or(errno::EINVAL)?;

    // 跨设备: 两边挂载点不同则不可 rename(git 收到 EXDEV 会退化成 copy+delete)
    let mp_old = mount::mount_point_of(&old_abs).ok_or(errno::ENOENT)?;
    let (mp_new, inner_new) = mount::mount_info_of(&new_abs).ok_or(errno::ENOENT)?;
    if mp_old != mp_new {
        return Err(errno::EXDEV);
    }

    // 防自环: 不允许 "/a" -> "/a/b"
    if new_abs.starts_with(&format!("{}/", old_abs)) {
        return Err(errno::EINVAL);
    }

    let (old_parent, old_name) = split_parent(&old_abs);
    let (new_parent, _) = split_parent(&new_abs);

    let old_parent_inode = path::lookup_abs_path(&old_parent).ok_or(errno::ENOENT)?;
    let new_parent_inode = path::lookup_abs_path(&new_parent).ok_or(errno::ENOENT)?;

    // 在源目录与目标目录中改名/创建 都需要父目录的 写+搜索 权限(root 绕过)
    if !check_dir_write(&old_parent_inode) || !check_dir_write(&new_parent_inode) {
        return Err(errno::EACCES);
    }
    // 下层各 fs 的 rename 约定: to 是"相对该 fs 根"的路径, 各 fs 自己走父子链。
    // 只剥挂载点前缀不够 —— bind 出来的挂载点(/home ← /data/home)要补回 inner_base,
    // 否则 rsext4 会从 fs 根去找 "root/try_git", 而它在 fs 里其实是 "home/root/try_git"。
    let new_rel = {
        let suffix = new_abs
            .strip_prefix(&mp_new)
            .unwrap_or(&new_abs)
            .trim_start_matches('/');
        match (inner_new.is_empty(), suffix.is_empty()) {
            (true, _) => String::from(suffix),
            (false, true) => inner_new.clone(),
            (false, false) => alloc::format!("{}/{}", inner_new, suffix),
        }
    };

    match old_parent_inode.rename(&old_name, &new_rel) {
        0 => Ok(()),
        // 下层只给出 0 / -1, 拿不到更细的原因; 把 -errno 原样上报(不再假装 EPERM)
        r if r < 0 => Err(-r),
        _ => Err(errno::EIO),
    }
}