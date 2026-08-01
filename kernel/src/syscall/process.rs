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

/// 单调时间(微秒, 自启动起)——ntpdate 的本地假时钟源。
pub fn sys_get_time_us() -> isize {
    crate::timer::monotonic_us() as isize
}

/// 校准墙钟: epoch_us = 此刻的绝对 Unix 时间(微秒)。
pub fn sys_set_wall_clock(epoch_us: usize) -> isize {
    crate::timer::set_wall_clock(epoch_us as u64);
    0
}

/// 墙钟秒(epoch)。未校准(没跑 ntpdate)返回 0。
pub fn sys_get_epoch() -> isize {
    crate::timer::now_secs() as isize
}

pub fn sys_kill(pid: usize, sig : usize) -> isize {
    crate::task::kill(pid, sig)
}

/// signal(sig, action): 设置信号处置(0=SIG_DFL 1=SIG_IGN), 返回旧处置或 -1。
pub fn sys_signal(sig: usize, action: usize) -> isize {
    crate::task::sig_set(sig, action)
}

/// set_front(pid): 把 pid 设为前台进程(Ctrl+C 投递目标)。
pub fn sys_set_front(pid: usize) -> isize {
    crate::task::set_front(pid);
    0
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

// ===== 进程凭证 / 权限系统 syscall handlers =====
//
// 凭证模型(简化版,不实现 saved-uid/saved-gid):
//   - 特权判定: euid == 0 即为超级用户(root)。
//   - setuid/seteuid/setgid/setegid 以及 setreuid/setregid 遵循以下规则:
//       * 特权进程(euid==0):可把 uid/euid/gid/egid 设成任意值。
//       * 非特权进程:只能把某字段设成自己当前的 uid / euid(或 gid / egid)。
//         否则返回 -1。
//   - usize::MAX 在 setreuid/setregid 中表示"不改变该字段"(对应 POSIX 的 -1)。

const NO_CHANGE: usize = usize::MAX;

/// (uid, euid, gid, egid) -> (new_uid, new_euid)，按 setuid 语义计算。
fn compute_setuid(cur: (usize, usize, usize, usize), uid: usize) -> Option<(usize, usize)> {
    let (cur_uid, cur_euid, _, _) = cur;
    if cur_euid == 0 || uid == cur_uid || uid == cur_euid {
        Some((uid, uid))
    } else {
        None
    }
}

/// (uid, euid, gid, egid) -> new_euid，按 seteuid 语义计算。
fn compute_seteuid(cur: (usize, usize, usize, usize), euid: usize) -> Option<usize> {
    let (cur_uid, cur_euid, _, _) = cur;
    if cur_euid == 0 || euid == cur_uid || euid == cur_euid {
        Some(euid)
    } else {
        None
    }
}

/// (uid, euid, gid, egid) -> (new_gid, new_egid)，按 setgid 语义计算。
fn compute_setgid(cur: (usize, usize, usize, usize), gid: usize) -> Option<(usize, usize)> {
    let (_, _, cur_gid, cur_egid) = cur;
    if cur_egid == 0 || gid == cur_gid || gid == cur_egid {
        Some((gid, gid))
    } else {
        None
    }
}

/// (uid, euid, gid, egid) -> new_egid，按 setegid 语义计算。
fn compute_setegid(cur: (usize, usize, usize, usize), egid: usize) -> Option<usize> {
    let (_, _, cur_gid, cur_egid) = cur;
    if cur_egid == 0 || egid == cur_gid || egid == cur_egid {
        Some(egid)
    } else {
        None
    }
}

pub fn sys_getuid() -> isize {
    crate::task::current_creds().0 as isize
}

pub fn sys_geteuid() -> isize {
    crate::task::current_creds().1 as isize
}

pub fn sys_getgid() -> isize {
    crate::task::current_creds().2 as isize
}

pub fn sys_getegid() -> isize {
    crate::task::current_creds().3 as isize
}

pub fn sys_setuid(uid: usize) -> isize {
    let cur = crate::task::current_creds();
    match compute_setuid(cur, uid) {
        Some((new_uid, new_euid)) => {
            let (_, _, g, ge) = cur;
            crate::task::set_current_creds(new_uid, new_euid, g, ge);
            0
        }
        None => -1,
    }
}

pub fn sys_seteuid(euid: usize) -> isize {
    let cur = crate::task::current_creds();
    match compute_seteuid(cur, euid) {
        Some(new_euid) => {
            let (u, _, g, ge) = cur;
            crate::task::set_current_creds(u, new_euid, g, ge);
            0
        }
        None => -1,
    }
}

pub fn sys_setgid(gid: usize) -> isize {
    let cur = crate::task::current_creds();
    match compute_setgid(cur, gid) {
        Some((new_gid, new_egid)) => {
            let (u, ue, _, _) = cur;
            crate::task::set_current_creds(u, ue, new_gid, new_egid);
            0
        }
        None => -1,
    }
}

pub fn sys_setegid(egid: usize) -> isize {
    let cur = crate::task::current_creds();
    match compute_setegid(cur, egid) {
        Some(new_egid) => {
            let (u, ue, g, _) = cur;
            crate::task::set_current_creds(u, ue, g, new_egid);
            0
        }
        None => -1,
    }
}

pub fn sys_setreuid(ruid: usize, euid: usize) -> isize {
    let cur = crate::task::current_creds();
    let mut new_uid = cur.0;
    let mut new_euid = cur.1;

    if ruid != NO_CHANGE {
        match compute_setuid(cur, ruid) {
            Some((u, _)) => new_uid = u,
            None => return -1,
        }
    }
    if euid != NO_CHANGE {
        match compute_seteuid(cur, euid) {
            Some(e) => new_euid = e,
            None => return -1,
        }
    }

    let (_, _, g, ge) = cur;
    crate::task::set_current_creds(new_uid, new_euid, g, ge);
    0
}

pub fn sys_setregid(rgid: usize, egid: usize) -> isize {
    let cur = crate::task::current_creds();
    let mut new_gid = cur.2;
    let mut new_egid = cur.3;

    if rgid != NO_CHANGE {
        match compute_setgid(cur, rgid) {
            Some((g, _)) => new_gid = g,
            None => return -1,
        }
    }
    if egid != NO_CHANGE {
        match compute_setegid(cur, egid) {
            Some(e) => new_egid = e,
            None => return -1,
        }
    }

    let (u, ue, _, _) = cur;
    crate::task::set_current_creds(u, ue, new_gid, new_egid);
    0
}

// getgroups(size, list):
//   - size == 0: 返回当前附加组数量(不含主组)。
//   - size >= ngroups: 把最多 size 个组 id 写入 list(用户态 usize 数组),
//     返回实际数量 ngroups。
//   - size < ngroups: 缓冲不足, 返回 -1。
// setgroups(size, list):
//   - 仅特权进程(euid == 0)可调用; 非特权返回 -1。
//   - size == 0 清空附加组; 否则用 list 前 size 个组 id 覆盖。
//   - size 超过 NGROUPS_MAX 视为非法返回 -1。

pub fn sys_getgroups(size: usize, list_ptr: usize) -> isize {
    let (groups, ngroups) = crate::task::current_groups();
    if size == 0 {
        return ngroups as isize;
    }
    if size < ngroups {
        return -1;
    }
    let mut buf = [0u8; crate::task::NGROUPS_MAX * 8];
    let mut off = 0;
    for &g in &groups[..ngroups] {
        buf[off..off + 8].copy_from_slice(&g.to_ne_bytes());
        off += 8;
    }
    match crate::task::write_current_user_bytes(list_ptr, &buf[..off]) {
        Some(_) => ngroups as isize,
        None => -1,
    }
}

pub fn sys_setgroups(size: usize, list_ptr: usize) -> isize {
    let (_u, euid, _g, _ge) = crate::task::current_creds();
    if euid != 0 {
        return -1; // 仅特权进程可设置附加组
    }
    if size > crate::task::NGROUPS_MAX {
        return -1;
    }
    if size == 0 {
        crate::task::set_current_groups([0usize; crate::task::NGROUPS_MAX], 0);
        return 0;
    }
    let need = size * core::mem::size_of::<usize>();
    let bytes = match crate::task::read_current_user_bytes(list_ptr, need) {
        Some(b) => b,
        None => return -1,
    };
    let mut groups = [0usize; crate::task::NGROUPS_MAX];
    let mut i = 0;
    while i < size {
        let off = i * core::mem::size_of::<usize>();
        let mut arr = [0u8; 8];
        arr.copy_from_slice(&bytes[off..off + 8]);
        groups[i] = usize::from_ne_bytes(arr);
        i += 1;
    }
    crate::task::set_current_groups(groups, size);
    0
}
