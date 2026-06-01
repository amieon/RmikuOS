use alloc::vec;

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

    let file = match crate::fs::open(path) {
        Some(file) => file,
        None => {
            log::warn!("[fs] open failed: {}", path);
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