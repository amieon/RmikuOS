use alloc::{format, vec};

use crate::fs;

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

    let mut kbuf = vec![0u8; len];

    let n = file.read(&mut kbuf);

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

pub fn sys_open(path_ptr: usize, len: usize) -> isize {
    let path_bytes = match crate::task::read_current_user_bytes(path_ptr, len) {
        Some(bytes) => bytes,
        None => return -1,
    };

    let path = match core::str::from_utf8(&path_bytes) {
        Ok(s) => s.trim_matches('\0').trim(),
        Err(_) => return -1,
    };

    let cwd = crate::task::current_cwd();

    let file = match crate::fs::open_at(&cwd, path) {
        Some(file) => file,
        None => {
            log::warn!("[fs] open failed: cwd={}, path={}", cwd, path);
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

pub fn sys_mkdir(path_ptr : usize, len : usize) -> isize {
    let path_bytes = match crate::task::read_current_user_bytes(path_ptr, len) {
        Some(bytes) => bytes,
        None => return -1,
    };

    let path = match core::str::from_utf8(&path_bytes) {
        Ok(s) => s.trim_matches('\0').trim(),
        Err(_) => return -1,
    };

    let cwd = crate::task::current_cwd();

    let abs = match crate::fs::normalize_path(&cwd, path) {
        Some(p) => p,
        None => return -1,
    };

    match crate::fs::make_dir(&abs) {
        Some(_) => 0,
        None => -1,
    }
}

pub fn sys_create(path_ptr : usize, len : usize) -> isize {
    let path_bytes = match crate::task::read_current_user_bytes(path_ptr, len) {
        Some(bytes) => bytes,
        None => return -1,
    };

    let path = match core::str::from_utf8(&path_bytes) {
        Ok(s) => s.trim_matches('\0').trim(),
        Err(_) => return -1,
    };

    let cwd = crate::task::current_cwd();

    let abs = match crate::fs::normalize_path(&cwd, path) {
        Some(p) => p,
        None => return -1,
    };

    match crate::fs::create_file(&abs) {
        Some(_) => 0,
        None => -1,
    }
}