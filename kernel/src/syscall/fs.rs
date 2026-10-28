use alloc::{format, string::String, vec, vec::Vec};
use core::sync::atomic::{AtomicUsize, Ordering};

use crate::fs;

const POLLFD_SIZE: usize = 8; // fd(i32) + events(i16) + revents(i16)
const MAX_POLL_FDS: usize = 1024;
static POLL_LOGGED: AtomicUsize = AtomicUsize::new(0);

#[cfg(target_arch = "riscv64")]
fn poll_now_ms() -> u64 {
    (crate::timer::monotonic_time() / 10_000) as u64
}

#[cfg(target_arch = "loongarch64")]
fn poll_now_ms() -> u64 {
    (crate::timer::monotonic_time() / 100_000) as u64
}

/// poll(fds, nfds, timeout_ms) -> 就绪 fd 数 / 0(超时) / -1。
/// timeout_ms: 0=立即返回,usize::MAX=无限等待(用户态 timeout=-1 映射)。
/// 这里没有等待队列:每轮扫描后让出 CPU,靠定时器节拍驱动网卡 poll。
pub fn sys_poll(fds_ptr: usize, nfds: usize, timeout_ms: usize) -> isize {
    if nfds > MAX_POLL_FDS || (nfds != 0 && fds_ptr == 0) {
        return -1;
    }

    let byte_len = match nfds.checked_mul(POLLFD_SIZE) {
        Some(n) => n,
        None => return -1,
    };
    let mut raw = if byte_len == 0 {
        Vec::new()
    } else {
        match crate::task::read_current_user_bytes(fds_ptr, byte_len) {
            Some(bytes) if bytes.len() == byte_len => bytes,
            _ => return -1,
        }
    };

    let infinite = timeout_ms == usize::MAX;
    let deadline = if infinite {
        u64::MAX
    } else {
        poll_now_ms().saturating_add(timeout_ms as u64)
    };

    loop {
        let mut ready = 0isize;
        for i in 0..nfds {
            let off = i * POLLFD_SIZE;
            let fd = i32::from_ne_bytes(raw[off..off + 4].try_into().unwrap());
            let events = i16::from_ne_bytes(raw[off + 4..off + 6].try_into().unwrap());
            let mut revents = 0i16;

            // POSIX:fd 为负数表示忽略这一项(常用于临时摘掉某个槽)。
            if fd >= 0 {
                revents = match crate::task::current_file(fd as usize) {
                    Some(file) => file.poll_ready(events),
                    None => fs::POLLNVAL,
                };
            }
            raw[off + 6..off + 8].copy_from_slice(&revents.to_ne_bytes());
            if revents != 0 {
                ready += 1;
            }
        }

        if ready != 0 || timeout_ms == 0 || (!infinite && poll_now_ms() >= deadline) {
            if byte_len != 0
                && crate::task::write_current_user_bytes(fds_ptr, &raw).is_none()
            {
                return -1;
            }
            if POLL_LOGGED.fetch_add(1, Ordering::Relaxed) < 8 {
                log::info!(
                    "[poll] nfds={} timeout_ms={} ready={}",
                    nfds, timeout_ms, ready
                );
            }
            return ready;
        }

        crate::drivers::net::maybe_poll();
        crate::task::suspend_current_and_run_next();
    }
}

pub fn sys_read(fd: usize, user_buf: usize, len: usize) -> isize {
    if len == 0 {
        return 0;
    }

    let file = match crate::task::current_file(fd) {
        Some(file) => file,
        None => return -super::errno::EBADF,
    };

    if !file.readable() {
        return -super::errno::EBADF;   /* 打开方式不对，Linux 也是 EBADF */
    }



    let flags = crate::task::get_fd_flags_current(fd);
    let nonblock = (flags & crate::fs::O_NONBLOCK) != 0;
    
    let mut kbuf = vec![0u8; len];

    let n = if nonblock {
        file.read_nonblock(&mut kbuf)
    } else {
        file.read(&mut kbuf)
    };

    if n <= 0 {
        return n;
    }

    let n = n as usize;

    if crate::task::write_current_user_bytes(user_buf, &kbuf[..n]).is_none() {
        return -super::errno::EFAULT;   /* 用户缓冲区不可写 */
    }

    n as isize
}

pub fn sys_pread(fd: usize, user_buf: usize, len: usize, offset: usize) -> isize {
    if len == 0 {
        return 0;
    }
    // 负 offset 传上来会是极大的 usize，提前拦掉（Linux 返回 EINVAL）
    if offset > isize::MAX as usize {
        return -1;
    }

    let file = match crate::task::current_file(fd) {
        Some(file) => file,
        None => return -1,
    };

    if !file.readable() {
        return -1;
    }

    let mut kbuf = vec![0u8; len];
    let n = file.read_at(&mut kbuf, offset);
    if n <= 0 {
        return n;
    }

    let n = n as usize;
    if crate::task::write_current_user_bytes(user_buf, &kbuf[..n]).is_none() {
        return -1;
    }

    n as isize
}


pub fn sys_pwrite(fd: usize, user_buf: usize, len: usize, offset: usize) -> isize {
    if len == 0 {
        return 0;
    }
    if offset > isize::MAX as usize {
        return -1;
    }

    let file = match crate::task::current_file(fd) {
        Some(file) => file,
        None => return -1,
    };

    if !file.writable() {
        return -1;
    }

    let kbuf = match crate::task::read_current_user_bytes(user_buf, len) {
        Some(buf) => buf,
        None => return -1,
    };

    file.write_at(&kbuf, offset)
}

pub fn sys_write(fd: usize, user_buf: usize, len: usize) -> isize {
    if len == 0 {
        return 0;
    }

    let file = match crate::task::current_file(fd) {
        Some(file) => file,
        None => return -super::errno::EBADF,
    };

    if !file.writable() {
        return -super::errno::EBADF;
    }

    let kbuf = match crate::task::read_current_user_bytes(user_buf, len) {
        Some(buf) => buf,
        None => return -1,
    };

    file.write(&kbuf)
}

/// open(path, len, flags, mode): mode 在 O_CREAT 创建文件时设权限位(POSIX)。
pub fn sys_open(path_ptr: usize, len: usize, flags: usize, mode: usize) -> isize {
    let path_bytes = match crate::task::read_current_user_bytes(path_ptr, len) {
        Some(bytes) => bytes,
        None => return -super::errno::EFAULT,
    };

    let path = match core::str::from_utf8(&path_bytes) {
        Ok(s) => s.trim_matches('\0').trim(),
        Err(_) => return -super::errno::EINVAL,
    };

    let cwd = crate::task::current_cwd();

    /* 失败要返回 -errno，不能返回裸 -1 —— 那等于 -EPERM，用户态会把
     * "没有这个文件"读成 "Operation not permitted"。
     * 这里也不打日志: open 试探是高频路径，串口只有 11.5K 字符/秒。 */
    let file = match crate::fs::open_at(&cwd, path, flags, mode) {
        Ok(file) => file,
        Err(e) => return -e,
    };

    let fd = crate::task::alloc_fd_current(file);

    if fd >= 0 {
        /* 记住访问模式 + O_APPEND/O_NONBLOCK —— `F_GETFL` 就报这个。
         * 不记的话 `fcntl(fd, F_GETFL) & O_ACCMODE` 永远报 O_RDONLY。 */
        crate::task::set_fd_flags_current(fd as usize, flags);

        /* O_CLOEXEC：一次系统调用就定好"别漏给子进程"，省掉 open+fcntl 之间
         * 那段被别的线程 fork 的竞态。 */
        if (flags & crate::fs::O_CLOEXEC) != 0 {
            crate::task::set_fd_cloexec_current(fd as usize, true);
        }
    }

    fd
}
pub fn sys_close(fd: usize) -> isize {
    crate::task::close_fd_current(fd)
}


pub fn sys_getdents(fd: usize, user_buf: usize, len: usize) -> isize {
    if len == 0 {
        return 0;
    }

    let file = match crate::task::current_file(fd) {
        Some(file) => file,
        None => return -1,
    };

    if !file.is_dir() {
        return -1;
    }

    let mut kbuf = alloc::vec![0u8; len];

    let n = file.getdents(&mut kbuf);

    if n <= 0 {
        return n;
    }

    let n = n as usize;

    if crate::task::write_current_user_bytes(user_buf, &kbuf[..n]).is_none() {
        return -1;
    }

    n as isize
}


pub fn sys_chdir(path_ptr: usize, len: usize) -> isize {
    let path_bytes = match crate::task::read_current_user_bytes(path_ptr, len) {
        Some(bytes) => bytes,
        None => return -1,
    };

    let path = match core::str::from_utf8(&path_bytes) {
        Ok(s) => s.trim_matches('\0').trim(),
        Err(_) => return -1,
    };

    let cwd = crate::task::current_cwd();

    let new_cwd = match crate::fs::normalize_path(&cwd, path) {
        Some(path) => path,
        None => return -1,
    };

    let inode = match crate::fs::lookup(&new_cwd) {
        Some(inode) => inode,
        None => {
            log::warn!("[fs] chdir failed: no such dir {}", new_cwd);
            return -1;
        }
    };

    if !inode.is_dir() {
        log::warn!("[fs] chdir failed: not dir {}", new_cwd);
        return -1;
    }

    crate::task::set_current_cwd(new_cwd)
}

pub fn sys_getcwd(user_buf: usize, len: usize) -> isize {
    if len == 0 {
        return -1;
    }

    let cwd = crate::task::current_cwd();
    let bytes = cwd.as_bytes();

    /*
     * 写入 cwd + '\0'
     */
    if bytes.len() + 1 > len {
        return -1;
    }

    if crate::task::write_current_user_bytes(user_buf, bytes).is_none() {
        return -1;
    }

    if crate::task::write_current_user_bytes(user_buf + bytes.len(), &[0]).is_none() {
        return -1;
    }

    bytes.len() as isize
}



fn write_stat_to_user(user_ptr: usize, stat: &crate::fs::Stat) -> isize {
    if user_ptr == 0 {
        return -1;
    }

    let bytes = unsafe {
        core::slice::from_raw_parts(
            stat as *const crate::fs::Stat as *const u8,
            core::mem::size_of::<crate::fs::Stat>(),
        )
    };

    if crate::task::write_current_user_bytes(user_ptr, bytes).is_none() {
        return -1;
    }

    0
}

pub fn sys_stat(path_ptr: usize, path_len: usize, stat_ptr: usize) -> isize {
    let path_bytes = match crate::task::read_current_user_bytes(path_ptr, path_len) {
        Some(bytes) => bytes,
        None => return -super::errno::EFAULT,
    };

    let path = match core::str::from_utf8(&path_bytes) {
        Ok(s) => s.trim_matches('\0').trim(),
        Err(_) => return -super::errno::EINVAL,
    };

    let cwd = crate::task::current_cwd();

    /* 失败要给 -errno: git 的 real_path_internal 靠 `errno == ENOENT` 判断
     * "最后一级允许不存在"。返回裸 -1(=EPERM) 会让 `git init` 在建 .git
     * 之前就误判成权限问题。 */
    let stat = match crate::fs::stat_at(&cwd, path) {
        Some(stat) => stat,
        None => return -super::errno::ENOENT,
    };

    write_stat_to_user(stat_ptr, &stat)
}

pub fn sys_fstat(fd: usize, stat_ptr: usize) -> isize {
    let file = match crate::task::current_file(fd) {
        Some(file) => file,
        // isatty 就靠这个 EBADF 区分"fd 没开"和"不是终端"
        None => return -super::errno::EBADF,
    };

    let stat = file.stat();

    write_stat_to_user(stat_ptr, &stat)
}

pub fn sys_lstat(path_ptr: usize, path_len: usize, stat_ptr: usize) -> isize {
    let path_bytes = match crate::task::read_current_user_bytes(path_ptr, path_len) {
        Some(bytes) => bytes,
        None => return -super::errno::EFAULT,
    };

    let path = match core::str::from_utf8(&path_bytes) {
        Ok(s) => s.trim_matches('\0').trim(),
        Err(_) => return -super::errno::EINVAL,
    };

    let cwd = crate::task::current_cwd();
    let stat = match crate::fs::lstat_at(&cwd, path) {
        Some(stat) => stat,
        None => return -super::errno::ENOENT,
    };

    write_stat_to_user(stat_ptr, &stat)
}

pub fn sys_pipe(fd : usize) -> isize {
    crate::task::new_pipe(fd)
}

pub fn sys_dup2(old_fd : usize,new_fd : usize) -> isize {
    crate::task::dup2(old_fd,new_fd)
}

pub fn sys_dup(fd: usize) -> isize {
    crate::task::dup(fd)
}

/// dup3(fd, newfd, flags)：dup2 加上 flags。
///
/// 只认 `O_CLOEXEC`（其余位返回 EINVAL）。和 dup2 的两点差别：
///   - `old_fd == new_fd` 必须是 EINVAL（dup2 是「什么都不做、返回成功」）；
///   - 新 fd 的描述符标志由 flags 决定，不从旧 fd 继承。
pub fn sys_dup3(old_fd: usize, new_fd: usize, flags: usize) -> isize {
    let unknown = flags & !crate::fs::O_CLOEXEC;
    if unknown != 0 {
        return -super::errno::EINVAL;
    }
    if old_fd == new_fd {
        return -super::errno::EINVAL;
    }

    let r = crate::task::dup2(old_fd, new_fd);
    if r < 0 {
        return r;
    }

    if (flags & crate::fs::O_CLOEXEC) != 0 {
        crate::task::set_fd_cloexec_current(new_fd, true);
    }
    r
}


pub fn sys_fchmod(fd: usize, mode: usize) -> isize {
    let file = match crate::task::current_file(fd) {
        Some(f) => f,
        None => return -1,
    };
    let inode = match file.inode() {
        Some(i) => i,
        None => return -1,
    };
    let meta = inode.metadata();
    let (_u, ue, _g, _ge) = crate::task::current_creds();
    if ue != 0 && ue != meta.uid {
        return -1;
    }
    inode.chmod((mode & 0o7777) as u16)
}

fn user_raw_string(ptr: usize, len: usize) -> Option<String> {
    let bytes = crate::task::read_current_user_bytes(ptr, len)?;
    let s = core::str::from_utf8(&bytes).ok()?;
    Some(String::from(s.trim_matches('\0')))
}

fn user_path_to_abs(path_ptr : usize, len : usize) -> Option<String>{
    let path_bytes = match crate::task::read_current_user_bytes(path_ptr, len) {
        Some(bytes) => bytes,
        None => return None,
    };

    let path = match core::str::from_utf8(&path_bytes) {
        Ok(s) => s.trim_matches('\0').trim(),
        Err(_) => return None,
    };

    let cwd = crate::task::current_cwd();

    let abs = match crate::fs::normalize_path(&cwd, path) {
        Some(p) => p,
        None => return None,
    };
    Some(abs)
}

pub fn sys_mkdir(path_ptr : usize, len : usize) -> isize {

    let abs = match user_path_to_abs (path_ptr, len) {
        Some(abs) => abs,
        None => return -1,
    };

    match crate::fs::make_dir(&abs) {
        Some(_) => 0,
        None => -1,
    }
}

pub fn sys_create(path_ptr : usize, len : usize) -> isize {
    let abs = match user_path_to_abs (path_ptr, len) {
        Some(abs) => abs,
        None => return -1,
    };

    match crate::fs::create_file(&abs) {
        Ok(_) => 0,
        Err(e) => -e,
    }
}

pub fn sys_rmdir(path_ptr : usize, len : usize) -> isize {

    let abs = match user_path_to_abs (path_ptr, len) {
        Some(abs) => abs,
        None => return -1,
    };

    match crate::fs::remove_dir(&abs) {
        Some(ret) => {
            match ret {
                0 => 0,
                _ => -1,
            }
        },
        None => -1,
    }
}

pub fn sys_remove_recursive(path_ptr : usize, len : usize) -> isize {
    let abs = match user_path_to_abs (path_ptr, len) {
        Some(abs) => abs,
        None => return -1,
    };

    match crate::fs::remove_recursive(&abs) {
        Some(ret) => {
            match ret {
                0 => 0,
                _ => -1,
            }
        },
        None => -1,
    }
}

pub fn sys_unlink(path_ptr : usize, len : usize) -> isize {

    let abs = match user_path_to_abs (path_ptr, len) {
        Some(abs) => abs,
        None => return -1,
    };

    match crate::fs::unlink_file(&abs) {
        Some(ret) => {
            match ret {
                0 => 0,
                _ => -1,
            }
        },
        None => -1,
    }
}

/// symlink(target, linkpath): target 原样保存，linkpath 按 cwd 解析。
pub fn sys_symlink(target_ptr: usize, target_len: usize, path_ptr: usize, path_len: usize) -> isize {
    let target = match user_raw_string(target_ptr, target_len) {
        Some(target) => target,
        None => return -1,
    };
    let path = match user_path_to_abs(path_ptr, path_len) {
        Some(path) => path,
        None => return -1,
    };

    match crate::fs::symlink_file(&target, &path) {
        Some(_) => 0,
        None => -1,
    }
}

/// readlink(path, buf, bufsiz): 返回实际写入字节数，不写 NUL。
pub fn sys_readlink(path_ptr: usize, path_len: usize, buf: usize, bufsiz: usize) -> isize {
    let path = match user_path_to_abs(path_ptr, path_len) {
        Some(path) => path,
        None => return -1,
    };
    let target = match crate::fs::readlink_path(&path) {
        Some(target) => target,
        None => return -1,
    };

    let copy_len = core::cmp::min(bufsiz, target.len());
    if copy_len == 0 {
        return 0;
    }
    if buf == 0 || crate::task::write_current_user_bytes(buf, &target.as_bytes()[..copy_len]).is_none() {
        return -1;
    }
    copy_len as isize
}

/// link(oldpath, newpath): 创建硬链接，不允许跨挂载点，源路径不跟随最终 symlink。
pub fn sys_link(old_ptr: usize, old_len: usize, new_ptr: usize, new_len: usize) -> isize {
    let old = match user_path_to_abs(old_ptr, old_len) {
        Some(path) => path,
        None => return -1,
    };
    let new = match user_path_to_abs(new_ptr, new_len) {
        Some(path) => path,
        None => return -1,
    };

    crate::fs::link_file(&old, &new)
}

pub fn sys_access(path_ptr: usize, len: usize, mode: usize) -> isize {
    let abs = match user_path_to_abs(path_ptr, len) {
        Some(abs) => abs,
        None => return -1,
    };
    crate::fs::access_path(&abs, (mode & 0x7) as u16)
}

pub fn sys_chmod(path_ptr: usize, len: usize, mode: usize) -> isize {
    let abs = match user_path_to_abs(path_ptr, len) {
        Some(abs) => abs,
        None => return -1,
    };

    let inode = match crate::fs::lookup(&abs) {
        Some(i) => i,
        None => return -1,
    };

    let meta = inode.metadata();
    let (_u, ue, _g, _ge) = crate::task::current_creds();

    // 特权: euid==0(超级用户) 或 调用者是文件属主
    if ue != 0 && ue != meta.uid {
        return -1;
    }

    crate::fs::chmod_path(&abs, (mode & 0o7777) as u16)
}

pub fn sys_chown(path_ptr: usize, len: usize, uid: usize, gid: usize) -> isize {
    let abs = match user_path_to_abs(path_ptr, len) {
        Some(abs) => abs,
        None => return -1,
    };

    let (_u, ue, _g, _ge) = crate::task::current_creds();

    // 简化模型: 仅超级用户(euid==0)可修改文件属主
    if ue != 0 {
        return -1;
    }

    crate::fs::chown_path(&abs, uid, gid)
}

/// fsync(fd): 把 fd 指向的文件数据刷盘。
pub fn sys_fsync(fd: usize) -> isize {
    let file = match crate::task::current_file(fd) {
        Some(file) => file,
        None => return -1,
    };
    file.fsync()
}

/// ftruncate(fd, len): 把已打开的文件截断为 len 字节。需以可写方式打开。
pub fn sys_ftruncate(fd: usize, len: usize) -> isize {
    let file = match crate::task::current_file(fd) {
        Some(file) => file,
        None => return -1,
    };
    if !file.writable() {
        return -1;
    }
    file.ftruncate(len)
}

/// truncate(path, len): 按路径把文件截断为 len 字节。
pub fn sys_truncate(path_ptr: usize, path_len: usize, len: usize) -> isize {
    let abs = match user_path_to_abs(path_ptr, path_len) {
        Some(abs) => abs,
        None => return -1,
    };
    crate::fs::truncate_path(&abs, len)
}

/// lseek(fd, offset, whence): 设置/查询文件读写偏移。
/// whence: 0=SEEK_SET, 1=SEEK_CUR, 2=SEEK_END。返回新的绝对偏移量。
pub fn sys_lseek(fd: usize, offset: isize, whence: usize) -> isize {
    let file = match crate::task::current_file(fd) {
        Some(file) => file,
        None => return -1,
    };
    file.seek(offset, whence)
}

/// rename(old, new): 移动/改名(同一文件系统内可跨目录)。
pub fn sys_rename(old_ptr: usize, old_len: usize, new_ptr: usize, new_len: usize) -> isize {
    let old = match user_path_to_abs(old_ptr, old_len) {
        Some(p) => p,
        None => return -1,
    };
    let new = match user_path_to_abs(new_ptr, new_len) {
        Some(p) => p,
        None => return -1,
    };
    crate::fs::rename(&old, &new)
}


/// set_echo(on): 开关终端回显(1=开 0=关)。RmikuOS shell 自带行编辑器,
/// 提示符期间关掉回显(它自己回显); 执行外部交互命令前打开。
pub fn sys_set_echo(on: usize) -> isize {
    crate::io::uart::set_echo(on != 0);
    0
}

/* 
 * 设计：本 VFS 是按"路径字符串"解析的——挂载表按路径前缀匹配、symlink 也按路径
 * 重新解析。所以 dirfd 不能像 Linux 那样从 dentry 往下走（一往下走就断了
 * 挂载点，例如 dirfd=/ + "fat/x" 会落到 rootfs 里那个空的 fat 目录，而不是 FAT 盘）。
 *
 * 做法：让目录 fd 自报绝对路径（File::abs_path，open 时写入），拼成绝对路径后
 * 完全复用现有实现。好处是 at 与非 at 的行为天然一致——同一套 lookup、
 * 挂载穿越、symlink 跟随逻辑，不需要给每个操作维护两份。
 *
 * 已知取舍：目录被 rename 之后，旧的 dirfd 仍指向旧路径（Linux 用 inode 就不会）。
 * 教学系统里可接受；若要修，得把 VFS 改成 dentry/inode 式解析。
 */

/// POSIX 的 AT_FDCWD：用进程 cwd，而不是某个 dirfd
pub const AT_FDCWD: isize = -100;
/// 不跟随最终 symlink（fstatat/fchmodat/faccessat 共用）
pub const AT_SYMLINK_NOFOLLOW: usize = 0x100;
/// unlinkat：目标是目录时删除目录（非空则失败）
pub const AT_REMOVEDIR: usize = 0x200;

/// faccessat 的 AT_EACCESS：注意它和 AT_REMOVEDIR 数值相同，但分属不同 syscall。
pub const AT_EACCESS: usize = 0x200;

/// 把 (dirfd, path) 解析成绝对路径。三条规则与 Linux 一致：
///   1. path 是绝对路径 → 忽略 dirfd（哪怕 dirfd 是无效的）
///   2. dirfd == AT_FDCWD → 相对进程 cwd
///   3. 否则 dirfd 必须是一个已打开的目录（普通文件/pipe 当 dirfd 会失败）
fn resolve_at(dirfd: isize, path: &str) -> Option<String> {
    if path.starts_with('/') {
        return crate::fs::normalize_path("/", path);
    }
    if dirfd == AT_FDCWD {
        return crate::fs::normalize_path(&crate::task::current_cwd(), path);
    }
    if dirfd < 0 {
        return None;
    }
    let file = crate::task::current_file(dirfd as usize)?;
    if !file.is_dir() {
        return None;                  // ENOTDIR: 不是目录 fd
    }
    let base = file.abs_path()?;      // 非目录 fd 拿不到路径，自然失败
    crate::fs::normalize_path(&base, path)
}

/// 读用户态路径 + dirfd 解析，一步到位
fn user_path_to_abs_at(dirfd: isize, path_ptr: usize, len: usize) -> Option<String> {
    let bytes = crate::task::read_current_user_bytes(path_ptr, len)?;
    let path = core::str::from_utf8(&bytes).ok()?.trim_matches('\0').trim();
    resolve_at(dirfd, path)
}

/// openat(dirfd, path, flags, mode)
///
/// 解析成绝对路径后走 `fs::open`（不是 `fs::open_at`，后者还要再叠一次 cwd）。
pub fn sys_openat(dirfd: usize, path_ptr: usize, len: usize, flags: usize, mode: usize) -> isize {
    let abs = match user_path_to_abs_at(dirfd as isize, path_ptr, len) {
        Some(abs) => abs,
        None => return -1,
    };
    let file = match crate::fs::open(&abs, flags, mode) {
        Ok(file) => file,
        Err(e) => return -e,
    };
    crate::task::alloc_fd_current(file)
}

/// fstatat(dirfd, path, statbuf, flags)
///
/// flags 带 AT_SYMLINK_NOFOLLOW 时等价于 lstat（看链接本身而不是目标）；
/// 否则等价于 stat。
pub fn sys_fstatat(dirfd: usize, path_ptr: usize, len: usize, stat_ptr: usize, flags: usize) -> isize {
    // 只支持 AT_SYMLINK_NOFOLLOW。AT_EMPTY_PATH / AT_NO_AUTOMOUNT 没有实现，
    // 明确拒绝好过按"跟随 symlink"的默认语义糊过去。
    if flags & !AT_SYMLINK_NOFOLLOW != 0 {
        return -super::errno::EINVAL;
    }
    let abs = match user_path_to_abs_at(dirfd as isize, path_ptr, len) {
        Some(abs) => abs,
        None => return -super::errno::EFAULT,
    };
    let stat = if flags & AT_SYMLINK_NOFOLLOW != 0 {
        crate::fs::lstat(&abs)
    } else {
        crate::fs::stat(&abs)
    };
    match stat {
        Some(stat) => write_stat_to_user(stat_ptr, &stat),
        None => -super::errno::ENOENT,
    }
}

/// unlinkat(dirfd, path, flags)
///
/// flags == 0        → 删文件（等价 unlink）
/// flags == AT_REMOVEDIR → 删目录（等价 rmdir，非空目录失败）
/// 其他位一律拒绝：POSIX 只允许这两个取值，多出来的位不该被静默忽略。
pub fn sys_unlinkat(dirfd: usize, path_ptr: usize, len: usize, flags: usize) -> isize {
    if flags != 0 && flags != AT_REMOVEDIR {
        return -1;
    }
    let abs = match user_path_to_abs_at(dirfd as isize, path_ptr, len) {
        Some(abs) => abs,
        None => return -1,
    };
    let ret = if flags & AT_REMOVEDIR != 0 {
        match crate::fs::remove_dir(&abs) {
            Some(r) => r,
            None => return -1,
        }
    } else {
        match crate::fs::unlink_file(&abs) {
            Some(r) => r,
            None => return -1,
        }
    };
    if ret == 0 { 0 } else { -1 }
}

/// mkdirat(dirfd, path, mode)
pub fn sys_mkdirat(dirfd: usize, path_ptr: usize, len: usize, mode: usize) -> isize {
    let abs = match user_path_to_abs_at(dirfd as isize, path_ptr, len) {
        Some(abs) => abs,
        None => return -1,
    };
    match crate::fs::make_dir(&abs) {
        Some(inode) => {
            // 与 open(O_CREAT) 一致：建完按 mode 落权限位
            let perm = crate::fs::mode_with_umask(mode);
            if inode.chmod(perm) != 0 {
                log::warn!("[fs] mkdirat: chmod(0o{:o}) failed on {}", perm, abs);
                return -1;
            }
            0
        }
        None => -1,
    }
}

/// faccessat(dirfd, path, amode, flags)
///
/// 默认用真实身份(ruid/rgid)，带 AT_EACCESS 时用有效身份(euid/egid)。
/// amode 与 access 一致：F_OK=0 / X_OK=1 / W_OK=2 / R_OK=4。
pub fn sys_faccessat(dirfd: usize, path_ptr: usize, len: usize, amode: usize, flags: usize) -> isize {
    // AT_SYMLINK_NOFOLLOW 需要"检查链接本身"，本内核的 access 一定跟随 symlink，
    // 做不到就明确拒绝（而不是悄悄按跟随语义给个结果）。
    if flags & AT_SYMLINK_NOFOLLOW != 0 {
        return -1;
    }
    let abs = match user_path_to_abs_at(dirfd as isize, path_ptr, len) {
        Some(abs) => abs,
        None => return -1,
    };
    let (ruid, euid, rgid, egid) = crate::task::current_creds();
    let (uid, gid) = if flags & AT_EACCESS != 0 {
        (euid, egid)
    } else {
        (ruid, rgid)
    };
    crate::fs::access_path_as(&abs, (amode & 0x7) as u16, uid, gid)
}

/// fchmodat(dirfd, path, mode, flags)
///
/// 特权检查与 chmod 相同：euid==0 或文件属主。AT_SYMLINK_NOFOLLOW 未实现——
/// 明确返回 -1，而不是"忽略 flag 当作已生效"。
pub fn sys_fchmodat(dirfd: usize, path_ptr: usize, len: usize, mode: usize, flags: usize) -> isize {
    if flags & AT_SYMLINK_NOFOLLOW != 0 {
        return -1;
    }
    let abs = match user_path_to_abs_at(dirfd as isize, path_ptr, len) {
        Some(abs) => abs,
        None => return -1,
    };
    let inode = match crate::fs::lookup(&abs) {
        Some(i) => i,
        None => return -1,
    };
    let meta = inode.metadata();
    let (_u, ue, _g, _ge) = crate::task::current_creds();
    if ue != 0 && ue != meta.uid {
        return -1;
    }
    crate::fs::chmod_path(&abs, (mode & 0o7777) as u16)
}

/// fchown(fd, uid, gid)：按 fd 改属主/属组。
///
/// 与 chown 的差别只有"用 fd 还是用路径"：省一次路径解析，也不受文件被改名影响。
/// 反查 inode 走 `File::inode()`（fchmod 时建的那条路），所以 pipe / 终端 /
/// 只读 rootfs 的内存副本这些"定位不到 inode"的 fd 一律 -1。
///
/// 特权模型与 chown 一致（简化）：仅 euid==0 可改。
/// 不支持 Linux 的 (uid_t)-1 哨兵（"这一项不动"），与现有 chown 的行为保持一致。
pub fn sys_fchown(fd: usize, uid: usize, gid: usize) -> isize {
    let file = match crate::task::current_file(fd) {
        Some(f) => f,
        None => return -1,
    };
    let inode = match file.inode() {
        Some(i) => i,
        None => return -1,
    };
    let (_u, ue, _g, _ge) = crate::task::current_creds();
    if ue != 0 {
        return -1;
    }
    inode.chown(uid, gid)
}

/// fchdir(fd)：把进程 cwd 切到 fd 指向的目录。
///
/// 相比 `chdir("路径")` 的价值：路径在"解析完到真正切换"之间可能被别的线程改掉，
/// 而 fd 指向的是已经打开的那个目录，没有这个窗口。
/// 本 VFS 存的是路径字符串，所以这里靠 `File::abs_path()` 取回绝对路径。
pub fn sys_fchdir(fd: usize) -> isize {
    let file = match crate::task::current_file(fd) {
        Some(f) => f,
        None => return -1,
    };
    if !file.is_dir() {
        return -1;                    // ENOTDIR: 只有目录 fd 能 chdir
    }
    let abs = match file.abs_path() {
        Some(p) => p,
        None => return -1,
    };
    // 目录可能在 fd 打开之后被删掉了（fd 还活着，但路径已失效）。
    // 重新解析一次再切，免得把进程 cwd 设成一个悬空路径——否则之后每个
    // 相对路径操作都会莫名其妙失败，排查起来很痛苦。
    // （Linux 允许 fchdir 到已删除的目录，因为它持有 inode；这里按路径实现，
    //   所以选择"宁可失败也不留悬空 cwd"。）
    let inode = match crate::fs::lookup(&abs) {
        Some(i) => i,
        None => return -1,
    };
    if !inode.is_dir() {
        return -1;
    }
    crate::task::set_current_cwd(abs)
}

/// renameat(olddirfd, old, newdirfd, new)：双路径各自相对自己的 dirfd。
///
/// 两侧独立解析，可以任意组合：`(dirfd, "a") → (AT_FDCWD, "/x/b")` 也合法。
/// 解析成两个绝对路径后交给 `fs::rename`——跨挂载点、目标存在时的覆盖语义都由
/// 底层统一处理，at 版本不重复实现一遍。
pub fn sys_renameat(d1: usize, old_ptr: usize, old_len: usize,
                    d2: usize, new_ptr: usize, new_len: usize) -> isize {
    let old = match user_path_to_abs_at(d1 as isize, old_ptr, old_len) {
        Some(p) => p,
        None => return -1,
    };
    let new = match user_path_to_abs_at(d2 as isize, new_ptr, new_len) {
        Some(p) => p,
        None => return -1,
    };
    crate::fs::rename(&old, &new)
}

/// linkat(olddirfd, old, newdirfd, new)：硬链接，两侧各自相对 dirfd。
/// 语义与link一致（源路径不跟随最终 symlink，不允许跨挂载点）。
pub fn sys_linkat(d1: usize, old_ptr: usize, old_len: usize,
                  d2: usize, new_ptr: usize, new_len: usize) -> isize {
    let old = match user_path_to_abs_at(d1 as isize, old_ptr, old_len) {
        Some(p) => p,
        None => return -1,
    };
    let new = match user_path_to_abs_at(d2 as isize, new_ptr, new_len) {
        Some(p) => p,
        None => return -1,
    };
    crate::fs::link_file(&old, &new)
}