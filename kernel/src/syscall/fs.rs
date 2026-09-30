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
        None => return -1,
    };

    if !file.readable() {
        return -1;
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
        return -1;
    }

    n as isize
}

pub fn sys_write(fd: usize, user_buf: usize, len: usize) -> isize {
    if len == 0 {
        return 0;
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

    file.write(&kbuf)
}

/// open(path, len, flags, mode): mode 在 O_CREAT 创建文件时设权限位(POSIX)。
pub fn sys_open(path_ptr: usize, len: usize, flags: usize, mode: usize) -> isize {
    let path_bytes = match crate::task::read_current_user_bytes(path_ptr, len) {
        Some(bytes) => bytes,
        None => return -1,
    };

    let path = match core::str::from_utf8(&path_bytes) {
        Ok(s) => s.trim_matches('\0').trim(),
        Err(_) => return -1,
    };

    let cwd = crate::task::current_cwd();

    let file = match crate::fs::open_at(&cwd, path, flags, mode) {
        Some(file) => file,
        None => {
            log::info!("[fs] open failed: cwd={}, path={}", cwd, path);
            return -1;
        }
    };

    crate::task::alloc_fd_current(file)
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
        None => return -1,
    };

    let path = match core::str::from_utf8(&path_bytes) {
        Ok(s) => s.trim_matches('\0').trim(),
        Err(_) => return -1,
    };

    let cwd = crate::task::current_cwd();

    let stat = match crate::fs::stat_at(&cwd, path) {
        Some(stat) => stat,
        None => {
            log::warn!("[fs] stat failed: cwd={}, path={}", cwd, path);
            return -1;
        }
    };

    write_stat_to_user(stat_ptr, &stat)
}

pub fn sys_fstat(fd: usize, stat_ptr: usize) -> isize {
    let file = match crate::task::current_file(fd) {
        Some(file) => file,
        None => return -1,
    };

    let stat = file.stat();

    write_stat_to_user(stat_ptr, &stat)
}

pub fn sys_pipe(fd : usize) -> isize {
    crate::task::new_pipe(fd)
}

pub fn sys_dup2(old_fd : usize,new_fd : usize) -> isize {
    crate::task::dup2(old_fd,new_fd)
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
        Some(_) => 0,
        None => -1,
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
