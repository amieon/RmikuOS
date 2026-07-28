use alloc::{string::String, vec::Vec};

pub fn sys_exit(exit_code: i32) -> ! {
    crate::task::exit_current_and_run_next(exit_code);
}

pub fn sys_yield() -> isize {
    crate::task::suspend_current_and_run_next()
}

pub fn sys_getpid() -> isize {
    crate::task::current_task_id() as isize
}

pub fn sys_getppid() -> isize {
    crate::task::current_task_ppid() as isize
}

pub fn sys_sleep(ticks: usize) -> isize {
    crate::task::sleep_current_and_run_next(ticks)
}

pub fn sys_waitpid(pid: isize, exit_code_ptr: usize, option : usize) -> isize {
    crate::task::waitpid_current(pid, exit_code_ptr, option)
}

pub fn sys_fork() -> isize {
    crate::task::fork_current()
}

pub fn sys_exec(path_ptr: usize, path_len: usize, args_ptr: usize) -> isize {
    crate::task::exec_current(path_ptr, path_len, args_ptr)
}

pub fn sys_mmap(len: usize, prot: usize) -> isize {
    crate::task::mmap_current(len, prot)
}


pub fn sys_munmap(addr: usize, len: usize) -> isize {
    crate::task::munmap_current(addr, len)
}

pub fn sys_set_process_tickets(tid : usize, tickets: usize) -> isize {
    crate::task::set_process_tickets_current(tid, tickets)
}

pub fn sys_set_my_tickets(tickets: usize) -> isize {
    crate::task::set_my_tickets_current(tickets)
}

pub fn sys_get_process_tickets(pid : usize) -> isize {
    crate::task::get_process_tickets_current(pid)
}

pub fn sys_get_my_tickets() -> isize {
    crate::task::get_my_tickets_current()
}

pub fn sys_set_sched_alpha(alpha: usize) -> isize {
    crate::task::set_sched_alpha_current(alpha.try_into().unwrap())
}

pub fn sys_get_sched_alpha() -> isize {
    crate::task::get_sched_alpha_current()
}

pub fn sys_get_process_sched_stat(pid: usize, stat_ptr: usize) -> isize {
    crate::task::get_process_sched_stat(pid, stat_ptr)
}

pub fn sys_reset_sched_stat() -> isize {
    crate::task::reset_sched_stat()
}

pub fn sys_get_ticks() -> isize {
    crate::timer::ticks().try_into().unwrap()
}

pub fn sys_kill(pid: usize, sig : usize) -> isize {
    crate::task::kill(pid, sig)
}

pub fn sys_fcntl(fd: usize, cmd: usize, arg: usize) -> isize {
    crate::task::set_fcntl(fd, cmd, arg)
}

// ===== 环境变量子系统 syscall handlers =====

/// getenv(key_ptr, key_len, buf_ptr, buf_len)
/// 返回值长度（不含 NUL），不存在返回 -1；buf=0 时仅探测长度。
pub fn sys_getenv(key_ptr: usize, key_len: usize, buf_ptr: usize, buf_len: usize) -> isize {
    let key_bytes = match crate::task::read_current_user_bytes(key_ptr, key_len) {
        Some(b) => b,
        None => return -1,
    };
    let key = match core::str::from_utf8(&key_bytes) {
        Ok(s) => s,
        Err(_) => return -1,
    };
    match crate::task::env_get_current_value(key) {
        Some(v) => {
            let bytes = v.as_bytes();
            if buf_ptr != 0 && buf_len >= bytes.len() {
                crate::task::write_current_user_bytes(buf_ptr, bytes);
            }
            bytes.len() as isize
        }
        None => -1,
    }
}

/// setenv(key_ptr, key_len, val_ptr, val_len, overwrite)
/// 成功返回 0，key 为空或非法返回 -1。
pub fn sys_setenv(
    key_ptr: usize,
    key_len: usize,
    val_ptr: usize,
    val_len: usize,
    overwrite: usize,
) -> isize {
    // 读取 key 字节并转为 String
    let key_bytes = match crate::task::read_current_user_bytes(key_ptr, key_len) {
        Some(b) => b,
        None => return -1,
    };
    let key_string = match String::from_utf8(key_bytes) {
        Ok(s) => s,
        Err(_) => return -1,   // 非 UTF-8 非法
    };
    if key_string.is_empty() {
        return -1;
    }

    // 读取 val，若长度为 0 则使用空字符串
    let val_string = if val_len == 0 {
        None
    } else {
        match crate::task::read_current_user_bytes(val_ptr, val_len) {
            Some(b) => match String::from_utf8(b) {
                Ok(s) => Some(s),
                Err(_) => return -1,
            },
            None => return -1,
        }
    };

    // 获取 &str 引用
    let val_ref = if let Some(s) = &val_string { s.as_str() } else { "" };

    // 调用底层函数，传递拥有所有权的字符串的引用
    crate::task::env_set_current_value(&key_string, val_ref, overwrite != 0)
}
/// unsetenv(key_ptr, key_len) -> 0
pub fn sys_unsetenv(key_ptr: usize, key_len: usize) -> isize {
    let key_bytes = match crate::task::read_current_user_bytes(key_ptr, key_len) {
        Some(b) => b,
        None => return -1,
    };
    let key_string = match String::from_utf8(key_bytes) {
        Ok(s) => s,
        Err(_) => return -1,   // 非 UTF-8 非法
    };
    if key_string.is_empty() {
        return -1;
    }
    crate::task::env_unset_current_value(key_string.as_str())
}

/// clearenv() -> 0
pub fn sys_clearenv() -> isize {
    crate::task::env_clear_current_values()
}

/// listenv(buf_ptr, buf_len) -> 总字节数（"KEY=VALUE\0..." 含结尾 NUL）；buf=0 时返回所需大小。
pub fn sys_listenv(buf_ptr: usize, buf_len: usize) -> isize {
    let pairs = crate::task::env_get_all_current();
    let mut total: usize = 0;
    for (k, v) in &pairs {
        // "KEY=VALUE\0"
        total = total
            .checked_add(k.len())
            .and_then(|t| t.checked_add(1))
            .and_then(|t| t.checked_add(v.len()))
            .and_then(|t| t.checked_add(1))
            .unwrap_or(usize::MAX);
    }
    if buf_ptr != 0 && buf_len >= total && total > 0 {
        let mut data: Vec<u8> = Vec::with_capacity(total);
        for (k, v) in &pairs {
            data.extend_from_slice(k.as_bytes());
            data.push(b'=');
            data.extend_from_slice(v.as_bytes());
            data.push(0);
        }
        crate::task::write_current_user_bytes(buf_ptr, &data);
    }
    total as isize
}
