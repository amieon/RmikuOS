use core::sync::atomic::{AtomicBool, AtomicUsize, Ordering};

use alloc::string::{String, ToString};
use alloc::vec::Vec;
use log::logger;

use crate::arch::{MAX_HARTS, ipi};                 
use crate::task::process::NGROUPS_MAX;
use crate::{lock_detect, println};
use crate::mm::{MemorySet, PhysPageNum, VirtAddr, PAGE_SIZE_BITS};
use crate::mm::config::PAGE_SIZE;
use crate::sync::spin::Mutex;
use crate::task::manager;
use crate::task::switch::switch_unlock_and_switch;
use crate::trap::TrapContext;

use super::context::TaskContext;
use super::process::{Pid, ProcessControlBlock};
use super::processor;
use super::signal::{DefAction, SigAction, SigFrame};
use super::switch::__switch;
use super::thread::{BlockReason, ThreadControlBlock, ThreadStatus, Tid};

use super::manager::{WaitPidAction, TaskManager};


unsafe extern "C" {
    fn __restore_user(cx: *const TrapContext) -> !;
}

static TASK_MANAGER: Mutex<TaskManager> = Mutex::new(TaskManager::new());

static PREEMPT_ENTER: AtomicUsize = AtomicUsize::new(0);
static PREEMPT_SWITCH_BACK: AtomicUsize = AtomicUsize::new(0);
static PREEMPT_ENTER_MASK: AtomicUsize = AtomicUsize::new(0);
static PREEMPT_BACK_MASK: AtomicUsize = AtomicUsize::new(0);

/// 空闲 hart 位图:进入 wfi 前置位,醒来后清除。
/// 新就绪的线程借此踢醒 idle hart,不再等它们的 timer 兜底。
static IDLE_HARTS: [AtomicBool; MAX_HARTS] =
    [const { AtomicBool::new(false) }; MAX_HARTS];

/// 若存在 idle hart,广播一次 Reschedule IPI。
/// (从简用广播;可改单播 + 按 hart 踢。)
fn kick_idle_harts() {
    for h in 0..MAX_HARTS {
        if IDLE_HARTS[h].load(Ordering::Acquire) {
            ipi::send_ipi_to_others(ipi::IpiKind::Reschedule, 0);
            return;
        }
    }
}

pub fn init() {
    let init_path = "/bin/login";

    let app = crate::fs::read_all(init_path)
        .expect("[task] failed to load init program (expected /bin/login)");

    let (user_space, entry, user_sp) =
        crate::mm::MemorySet::from_elf(app.as_slice()).unwrap();

    let trap_cx =
        crate::trap::TrapContext::app_init_context(entry, user_sp);

    let mut process = ProcessControlBlock::new(
        0,
        user_space,
        String::from("/"),
    );

    // init 进程以 root 身份启动：uid/euid/gid/egid 全为 0。
    process.uid = 0;
    process.euid = 0;
    process.gid = 0;
    process.egid = 0;

    // 给 init 进程(login, 以 root 身份)一份最小环境；之后可用 export 覆盖 / 追加。
    process.env.push((String::from("PATH"), String::from("/bin:/tests:/programs")));
    process.env.push((String::from("HOME"), String::from("/home")));
    process.env.push((String::from("PWD"), String::from("/")));
    // git 的 init 模板(hooks/*.sample、info/exclude)装在镜像里，但二进制里
    // 编译进去的路径是构建机的 $(HOME)/share/git-core/templates —— 交叉构建时
    // 那是宿主机路径，guest 里必然找不到。用环境变量指到镜像内的真实位置。
    process.env.push((
        String::from("GIT_TEMPLATE_DIR"),
        String::from("/usr/share/git-core/templates"),
    ));

    let thread = ThreadControlBlock::new_main_thread(
        0,
        0,
        trap_cx,
    );

    process.threads.push(0);
    process.ready_threads.push(0);
    process.runnable_count = 1;

    let mut manager = lock_detect!(TASK_MANAGER);
    manager.insert_process(process);
    manager.insert_thread(thread);

    log::info!("[task] loaded init shell as pid=0 tid=0");
}

pub fn run_first_task() -> ! {
    run_tasks()
}

pub fn run_tasks() -> ! {
    let hart = processor::current_hart_id();
    loop {
        crate::drivers::net::maybe_poll();
        // 合并取锁:唤醒睡眠者 + 检查进程定时器 + 选线程 + prepare 一次完成,
        // 每轮调度少抢两次全局锁。
        let mut itimer_woke = false;
        let next = {
            let mut manager = lock_detect!(TASK_MANAGER);  // lock 内部已 preempt_disable
            let now = crate::timer::ticks();
            manager.wake_sleeping_threads(now);
            /* ITIMER_REAL 到点 → SIGALRM。没有进程开过定时器时，这里只是一次
             * Option 比较（next_itimer_us 的早退缓存）。 */
            itimer_woke = manager.fire_due_itimers(crate::timer::monotonic_us());
            manager
                .find_next_ready_thread()
                .map(|tid| (tid, manager.prepare_thread(tid)))
        };  // 此处 drop guard → unlock → preempt_enable

        /* 定时器唤醒了别的 hart 上的线程？踢一下让它们去跑。 */
        if itimer_woke {
            kick_idle_harts();
        }

        if let Some((tid, (pid, root_ppn, kernel_stack_top, trap_cx_addr, task_cx_ptr))) = next {

            crate::task::processor::set_current_tid(Some(tid));
            crate::mm::activate_page_table(root_ppn);

            unsafe {
                switch_unlock_and_switch(task_cx_ptr);
            }   
            let hart = crate::arch::hartid();

            crate::mm::activate_kernel_page_table();
            crate::arch::flush_tlb();
            // mark_switch_back();
            // mark_switch_back_tick();

            // maybe_dump_smp_debug("switch-back");

            let pending_tid = crate::task::processor::take_pending_ready_tid();
            let mut need_ipi = false;

            {
                let mut manager = lock_detect!(TASK_MANAGER);

                let mut returned_status = None;
                let mut exited_pid = None;
                let mut exited_tid = None;

                if let Some(thread) = manager.try_thread_mut(tid) {
                    let pid = thread.pid;
                    let status = thread.status;

                    if thread.running_on == Some(hart) {
                        thread.running_on = None;
                    } else {
                        // log::error!(
                        //     "[sched] hart {} back from tid {}, but running_on={:?}",
                        //     hart,
                        //     tid,
                        //     thread.running_on,
                        // );
                        thread.running_on = None;
                    }

                    returned_status = Some(status);

                    if matches!(status, ThreadStatus::Zombie | ThreadStatus::Dead) {
                        exited_pid = Some(pid);
                        exited_tid = Some(tid);
                    }
                }

                if let Some(pending_tid) = pending_tid {
                    if manager.mark_thread_ready(pending_tid) {
                        //need_ipi = true;
                    }
                } else if returned_status == Some(ThreadStatus::Ready) {
                    manager.enqueue_ready_thread(tid);
                    //need_ipi = true;

                    log::info!(
                        "[sched] hart {} enqueue tid {} after early wake",
                        hart,
                        tid,
                    );
                }

                if let Some(pid) = exited_pid {
                    if manager.wake_parent_waiting_for(pid) {
                        need_ipi = true;
                    }
                }

                if let Some(tid) = exited_tid {
                    if manager.wake_threads_joining(tid) {
                        need_ipi = true;
                    }
                }
            }

            crate::task::processor::set_current_tid(None);
            crate::arch::enable_interrupt();

            if need_ipi {
                kick_idle_harts();
            }
        } else {
            crate::arch::disable_interrupt();

            let still_empty = {
                let mut manager = lock_detect!(TASK_MANAGER);
                let now = crate::timer::ticks();

                manager.wake_sleeping_threads(now);

                !manager.has_ready_thread()
            };

            if still_empty {
                IDLE_HARTS[hart].store(true, Ordering::Release);

                crate::arch::enable_interrupt();

                crate::drivers::net::poll();

                unsafe {
                    crate::arch::wait_for_interrupt();
                }

                IDLE_HARTS[hart].store(false, Ordering::Release);
            } else {
                crate::arch::enable_interrupt();
            }
        }
    }
}
#[no_mangle]
pub extern "C" fn __task_entry() -> ! {
    let current_tid = processor::current_tid();

    let trap_cx_addr = {
        let manager = lock_detect!(TASK_MANAGER);
        manager.thread(current_tid).trap_cx_addr
    };

    unsafe {
        __restore_user(trap_cx_addr as *const TrapContext);
    }
}

pub fn sleep_current_and_run_next(ticks: usize) -> isize {
    if ticks == 0 {
        return 0;
    }

    let current_tid = processor::current_tid();
    let wake_tick = crate::timer::ticks() + ticks;

    let task_cx_ptr = {
        let mut manager = lock_detect!(TASK_MANAGER);

        log::info!(
            "[task] thread {} sleep until tick {}",
            current_tid,
            wake_tick,
        );

        manager.block_thread(
            current_tid,
            BlockReason::Sleep {
                wake_tick,
            },
        );

        manager.thread_cx_ptr(current_tid)
    };

    let idle_cx_ptr = processor::idle_task_cx_ptr();
     
    // sleep 不会使其他线程就绪，无需 IPI
    unsafe {
        __switch(task_cx_ptr, idle_cx_ptr);
    }

    if crate::timer::ticks() < wake_tick {
        return -crate::syscall::errno::EINTR;
    }

    0
}

/// 设置当前进程的 `ITIMER_REAL`：`next_us` 是绝对单调微秒，0 = 关闭。
/// 返回旧的 `(next_us, interval_us)`，由 syscall 层换算成 `struct itimerval`。
pub fn set_itimer(next_us: u64, interval_us: u64) -> (u64, u64) {
    let pid = current_pid();
    let mut manager = lock_detect!(TASK_MANAGER);
    manager.set_itimer(pid, next_us, interval_us)
}

/// 查询当前进程的 `ITIMER_REAL`（同样是绝对单调微秒）。
pub fn get_itimer() -> (u64, u64) {
    let pid = current_pid();
    let manager = lock_detect!(TASK_MANAGER);
    manager.get_itimer(pid)
}

pub fn suspend_current_and_run_next() -> isize {
    let current_tid = processor::current_tid();

    let task_cx_ptr = {
        let mut manager = lock_detect!(TASK_MANAGER);

        // 这里只记录，不入 ready queue。
        // 真正 Ready 放到 run_tasks() 切回 scheduler 后做。
        processor::set_pending_ready_tid(current_tid);

        manager.thread_cx_ptr(current_tid)
    };

    let idle_cx_ptr = processor::idle_task_cx_ptr();

     
    crate::arch::disable_interrupt();

    unsafe {
        __switch(task_cx_ptr, idle_cx_ptr);
    }

    0
}

pub fn preempt_current_and_run_next() {
    let current_tid = match processor::current_tid_opt() {
        Some(tid) => tid,
        None => return,
    };

    //mark_preempt_enter();

    let task_cx_ptr = {
        let mut manager = match TASK_MANAGER.try_lock() {
            Some(guard) => guard,
            None => {
                // 拿不到锁不等于不用切:挂起延迟调度标记,
                // 由 trap 返回用户态前的出口检查补枪(否则静默白跑一个 slice)。
                processor::set_current_need_resched(true);
                return;
            }
        };

        processor::set_pending_ready_tid(current_tid);
        manager.thread_cx_ptr(current_tid)
    };

    let idle_cx_ptr = processor::idle_task_cx_ptr();

    crate::arch::disable_interrupt();

    unsafe {
        __switch(task_cx_ptr, idle_cx_ptr);
    }
}


pub fn waitpid_current(pid: isize, exit_code_ptr: usize, options: usize) -> isize {
    let current_tid = processor::current_tid();
    let nohang = (options & super::WNOHANG) != 0;

    loop {
        /* try_waitpid 必须和 block_thread 待在同一个锁区间：中间放掉锁的话，
         * 孩子刚好在这条缝里退出，`wake_parent_waiting_for` 会落空 ——
         * 于是父进程睡下去再也没人叫。 */
        const ACT_SLEEP: u8 = 0;
        const ACT_INTR: u8 = 1;
        const ACT_STOP: u8 = 2;

        let mut act = ACT_SLEEP;

        let task_cx_ptr = {
            let mut manager = lock_detect!(TASK_MANAGER);
            let current_pid = manager.pid_of_tid(current_tid);

            match manager.try_waitpid(current_pid, pid, exit_code_ptr, options) {
                WaitPidAction::Return(ret) => {
                    return ret;
                }

                WaitPidAction::Block => {
                    if nohang {
                        return 0;
                    }

                    /* 要睡了，先看有没有信号要处理。顺序是：数据（上面那步）
                     * → 停止 → EINTR → 真睡。把信号判断放在 try_waitpid 之前
                     * 会把"已经能收尸的孩子"让给信号，父进程白丢一次收尸机会。 */
                    if let Some(stop_sig) = manager.take_pending_stop(current_tid, current_pid) {
                        manager.stop_process(current_pid, stop_sig);
                        act = ACT_STOP;
                    } else if manager.deliverable_pending(current_tid, current_pid) {
                        act = ACT_INTR;
                    } else {
                        log::info!(
                            "[task] tid {} waitpid(pid={}) blocking",
                            current_tid,
                            pid,
                        );

                        manager.block_thread(
                            current_tid,
                            BlockReason::WaitPid { pid },
                        );
                    }

                    manager.thread_cx_ptr(current_tid)
                }
            }
        };

        /* POSIX: 被捕获的信号打断阻塞调用 → -1 + EINTR。
         * git 的 wait_or_whine、shell 的 `while (waitpid(...) < 0)` 都靠它重试。 */
        if act == ACT_INTR {
            return -crate::syscall::errno::EINTR;
        }

        /* 停止类信号就地停住（不返回 EINTR）：继续之后从 switch 返回，
         * 重新查一遍孩子状态 —— 程序完全看不出中间离开过。 */
        if act == ACT_STOP {
            switch_away_stopped();
            continue;
        }

        let idle_cx_ptr = processor::idle_task_cx_ptr();
         
        crate::arch::disable_interrupt();

        unsafe {
            __switch(task_cx_ptr, idle_cx_ptr);
        }
    }
}

pub fn fork_current() -> isize {
    let parent_tid = processor::current_tid();

    let child_pid = {
        let mut manager = lock_detect!(TASK_MANAGER);

        let parent_pid = manager.pid_of_tid(parent_tid);

        let child_pid = manager.alloc_pid();
        let child_tid = manager.alloc_tid();

        let child_user_space =
            MemorySet::from_existed_user(&manager.process(parent_pid).user_space);

        let mut child_trap_cx =
            *manager.thread(parent_tid).trap_cx();

        child_trap_cx.set_syscall_ret(0);

        let child_fd_table = manager.process(parent_pid).fd_table.clone();
        let child_fd_flags = manager.process(parent_pid).fd_flags.clone();
        let child_fd_cloexec = manager.process(parent_pid).fd_cloexec.clone();
        let child_free_fds = manager.process(parent_pid).free_fds.clone();
        for slot in child_fd_table.iter() {
            if let Some(file) = slot {
                file.on_fork();
            }
        }
        let child_cwd = manager.process(parent_pid).cwd.clone();

        let parent_tickets = manager.process(parent_pid).tickets;
        let parent_pass = manager.process(parent_pid).pass;

        let child_mmap_areas = manager.process(parent_pid).mmap_areas.clone();
        let child_mmap_free_areas = manager.process(parent_pid).mmap_free_ranges.clone();
        let child_mmap_next = manager.process(parent_pid).mmap_next.clone();


        let mut child_process = ProcessControlBlock::fork_from(
            child_pid,
            parent_pid,
            child_user_space,
            child_fd_table,
            child_fd_flags,
            child_fd_cloexec,
            child_free_fds,
            child_cwd,
            manager.process(parent_pid).env.clone(),
            parent_tickets,
            parent_pass,
            child_mmap_areas,
            child_mmap_free_areas,
            child_mmap_next,
            manager.process(parent_pid).uid,
            manager.process(parent_pid).euid,
            manager.process(parent_pid).gid,
            manager.process(parent_pid).egid,
            manager.process(parent_pid).groups,
            manager.process(parent_pid).ngroups,
            manager.process(parent_pid).umask,
            manager.process(parent_pid).pgid,
            manager.process(parent_pid).sid,
        );
        let child_thread = ThreadControlBlock::new_main_thread(
            child_tid,
            child_pid,
            child_trap_cx,
        );

        child_process.threads.push(child_tid);
        child_process.ready_threads.push(child_tid);

        manager.process_mut(parent_pid).children.push(child_pid);

        manager.insert_process(child_process);
        manager.insert_thread(child_thread);

        log::info!(
            "[task] fork: parent_pid={} parent_tid={} child_pid={} child_tid={}",
            parent_pid,
            parent_tid,
            child_pid,
            child_tid,
        );

        child_pid
    };

    ipi::send_ipi_to_others(ipi::IpiKind::Reschedule, 0);


    child_pid as isize
}



pub fn current_tid() -> Tid {
    processor::current_tid()
}

pub fn current_pid() -> Pid {
    let tid = processor::current_tid();
    let manager = lock_detect!(TASK_MANAGER);
    manager.pid_of_tid(tid)
}

pub fn current_ppid() -> Pid {
    let tid = processor::current_tid();
    let manager = lock_detect!(TASK_MANAGER);
    let pid = manager.pid_of_tid(tid);
    if let Some(ppid) = manager.process(pid).parent {
        ppid
    }else {
        0
    }
}

pub fn current_task_id() -> usize {
    current_pid()
}

pub fn current_task_ppid() -> usize {
    current_ppid()
}

/// 读取当前进程的 4 个凭证(uid, euid, gid, egid)。
///
/// boot 阶段（`primary_init` 里读 `/var/lib/rtcstamp`、建 `/data` 目录）还没有
/// current task。内核自己访问文件系统不属于任何进程，按 root 处理是唯一合理语义；
/// 这里如果走 `current_tid()` 就会 panic —— 一旦 stamp 文件存在，开机恢复墙钟时
/// `fs::open` → `check_open` → 这里，直接变成"开机只剩 boot.S 的 cCps 四个字符"。
pub fn current_creds() -> (usize, usize, usize, usize) {
    let tid = match processor::current_tid_opt() {
        Some(t) => t,
        None => return (0, 0, 0, 0),
    };
    let manager = lock_detect!(TASK_MANAGER);
    let pid = manager.pid_of_tid(tid);
    let p = manager.process(pid);
    (p.uid, p.euid, p.gid, p.egid)
}

/// 设置当前进程的 4 个凭证。
pub fn set_current_creds(uid: usize, euid: usize, gid: usize, egid: usize) {
    let tid = processor::current_tid();
    let mut manager = lock_detect!(TASK_MANAGER);
    let pid = manager.pid_of_tid(tid);
    let p = manager.process_mut(pid);
    p.uid = uid;
    p.euid = euid;
    p.gid = gid;
    p.egid = egid;
}

pub fn read_current_user_bytes(user_buf: usize, len: usize) -> Option<Vec<u8>> {
    let manager = lock_detect!(TASK_MANAGER);
    manager.read_current_user_bytes(user_buf, len)
}

pub fn write_current_user_bytes(user_buf: usize, data: &[u8]) -> Option<usize> {
    let pid = current_pid();
    let manager = lock_detect!(TASK_MANAGER);
    manager.write_user_bytes_by_pid(pid, user_buf, data)
}

pub fn wake_sleeping_tasks() {
    let now = crate::timer::ticks();
    {
        let mut manager = lock_detect!(TASK_MANAGER);
        manager.wake_sleeping_threads(now);
    }
    // 新增：被动唤醒超时线程，通知其他核
    ipi::send_ipi_to_others(ipi::IpiKind::Reschedule, 0);
}

const EXEC_MAX_ARGS: usize = 8;

#[repr(C)]
#[derive(Clone, Copy)]
struct UserArg {
    ptr: usize,
    len: usize,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct UserExecArgs {
    argc: usize,
    argv: [UserArg; EXEC_MAX_ARGS],
}

/// exec 失败的统一出口：必须返回真实 errno，不能一律 -1。
/// `-1` 就是 `-EPERM`，shell 会把它当成"permisssion denied"，
/// 而真正的原因（ENOENT/EACCES/ENOEXEC/ENOMEM）全被吞掉。
fn exec_fail(path: &str, why: &str, e: isize) -> isize {
    // warn 而不是 info：内核默认日志级别是 Warn（option_env!("LOG") 的兜底），
    // exec 失败本来就是异常事件、频次极低，用 info 等于把诊断彻底静音。
    log::warn!("[exec] {}: {} -> errno {}", why, path, e);
    -e
}

pub fn exec_current(path_ptr: usize, path_len: usize, args_ptr: usize) -> isize {
    let path_bytes = match crate::task::read_current_user_bytes(path_ptr, path_len) {
        Some(bytes) => bytes,
        None => return exec_fail("<path>", "path not readable", crate::syscall::errno::EFAULT),
    };

    let name = match core::str::from_utf8(&path_bytes) {
        Ok(s) => s.trim_matches('\0').trim(),
        Err(_) => return exec_fail("<path>", "path not utf8", crate::syscall::errno::ENOENT),
    };

    if name.is_empty() {
        return exec_fail("", "empty path", crate::syscall::errno::ENOENT);
    }

    let cwd = crate::task::current_cwd();
    let mut path_buf = String::new();

    let path = if name.starts_with('/') || name.starts_with("./") || name.starts_with("../") {
        match crate::fs::normalize_path(&cwd, name) {
            Some(p) => {
                path_buf = p;
                path_buf.as_str()
            }
            None => return exec_fail(name, "path normalize failed", crate::syscall::errno::ENOENT),
        }
    } else {
        path_buf.push_str("/bin/");
        path_buf.push_str(name);
        path_buf.as_str()
    };

    // exec 权限检查 + setuid/setgid 决策
    // - 需要文件的 X 权限(超级用户 euid==0 绕过)
    // - 若文件 mode 含 S_ISUID, 则 euid 提升为文件属主; S_ISGID 同理作用于 egid
    let elevate = {
        let (_u, euid, _g, egid) = crate::task::current_creds();
        let (groups, ngroups) = crate::task::current_groups();
        let meta = crate::fs::lookup(path).map(|i| i.metadata());
        if let Some(m) = meta {
            if !crate::fs::inode::check_access(
                m.mode, m.uid, m.gid, euid, egid, &groups[..ngroups], crate::fs::inode::X_OK,
            ) {
                log::warn!("[exec] permission denied (no exec bit): {}", path);
                return -crate::syscall::errno::EACCES;
            }
            let (u, ue, g, ge) = crate::task::current_creds();
            let new_ue = if m.mode & crate::fs::stat::S_ISUID != 0 { m.uid } else { ue };
            let new_ge = if m.mode & crate::fs::stat::S_ISGID != 0 { m.gid } else { ge };
            Some((u, new_ue, g, new_ge))
        } else {
            None
        }
    };

    let argv = match read_exec_args(args_ptr, name) {
        Ok(argv) => argv,
        Err(e) => return exec_fail(path, "bad exec args", e),
    };

    let argc = argv.len();

    // 把当前进程的 env 取出来，序列化后与 argv 一起压栈（execve 兼容的 envp）。
    let cur_tid = processor::current_tid();
    let env_pairs: Vec<(String, String)> = {
        let manager = lock_detect!(TASK_MANAGER);
        let pid = manager.pid_of_tid(cur_tid);
        manager.process(pid).env_pairs()
    };
    let env: Vec<Vec<u8>> = env_pairs
        .iter()
        .map(|(k, v)| {
            let mut s = String::with_capacity(k.len() + 1 + v.len());
            s.push_str(k);
            s.push('=');
            s.push_str(v);
            s.into_bytes()
        })
        .collect();

    let file = match crate::fs::open(path, crate::fs::flag::O_EXEC, 0) {
        Ok(file) => file,
        Err(e) => {
            // exec 的错误码必须准: shell 靠 ENOENT/EACCES 区分"命令不存在"与"不可执行"
            log::info!("[exec] open failed: {} (errno {})", path, e);
            return -e;
        }
    };

    if file.is_dir() || !file.readable() {
        log::warn!("[exec] not a regular readable file: {}", path);
        return exec_fail(path, "not a regular file", crate::syscall::errno::EACCES);
    }

    // 每次读 4 KiB。真正的批量在下面两层：ext4 的 CachedBlockReader 会把
    // 连续块合并成一次 fill_blocks，驱动再按 128 扇区（64 KiB）发一次请求。
    // 这里就是 4 KiB 的 syscall 分摊，再放大也不会改变底层请求次数。
    //
    // 缓冲放堆上：4 KiB 数组搁栈上在这个内核里没出过事，但 exec 是最深的一条
    // 调用链，没必要赌栈余量。
    let mut buf = alloc::vec![0u8; 4096];

    // 预分配：不预分配的话 10 MB 的 ELF 会反复 realloc + 整体搬迁，
    // 那是纯浪费（而且是"越到后面越慢"的那种）。
    let expect_size = file.stat().size;
    let capacity = if expect_size > 0 && expect_size < 64 * 1024 * 1024 {
        expect_size
    } else {
        64 * 1024
    };
    let mut app_data = Vec::with_capacity(capacity);

    // "开始加载"这条是为了能定位卡在 exec 里的情况：它和下面 exec-load 的
    // 统计行配成一对，中间没有第三行。串口最后一行是这条 → 卡在读 ELF；
    // 是 exec-load → exec 已经读完，问题在后面。
    if capacity >= 1024 * 1024 {
        log::info!("[exec] loading {} ({} KiB)", path, capacity / 1024);
    }

    loop {
        let n = file.read(&mut buf);

        if n < 0 {
            return exec_fail(path, "read failed", crate::syscall::errno::EIO);
        }

        if n == 0 {
            break;
        }

        app_data.extend_from_slice(&buf[..n as usize]);
    }

    // 大二进制（git 这种 10 MB 级）是块缓存的压力测试点：这一行直接给出
    // 命中率，用来判断扩容量到底值不值。小文件不打，省串口带宽。
    if app_data.len() >= 1024 * 1024 {
        crate::drivers::block::cache::log_stats("exec-load");
    }

    if app_data.is_empty() {
        return exec_fail(path, "empty executable", crate::syscall::errno::ENOEXEC);
    }

    let (new_user_space, entry, user_sp) =
    match crate::mm::MemorySet::from_elf(&app_data) {
        Some(v) => v,
        None => return exec_fail(path, "invalid ELF", crate::syscall::errno::ENOEXEC),
    };;

    let (new_user_sp, argv_ptr) = match build_user_stack_with_args(
        &new_user_space,
        user_sp,
        &argv,
        &env,
    ) {
        Some(v) => v,
        None => return exec_fail(path, "build user stack failed", crate::syscall::errno::ENOMEM),
    };


    let mut new_trap_cx =
        crate::trap::TrapContext::app_init_context(entry, new_user_sp);
    
    let envp = argv_ptr + (argc + 1) * core::mem::size_of::<usize>();
    new_trap_cx.set_app_args(argc, argv_ptr, envp);

    let current_tid = processor::current_tid();

    let new_root = {
        let mut manager = lock_detect!(TASK_MANAGER);
        let current_pid = manager.pid_of_tid(current_tid);

        if manager.process(current_pid).threads.len() != 1 {
            log::info!(
                "[exec] pid={} has multiple threads, exec denied in first version",
                current_pid,
            );
            return -crate::syscall::errno::EINVAL;
        }

        let (old_space, closed_files) = {
            let process = manager.process_mut(current_pid);
            let closed = process.close_non_standard_fds_on_exec();

            for a in process.sig_actions.iter_mut() {
                if let SigAction::Handler { .. } = a {
                    *a = SigAction::Default;
                }
            }
            process.sig_pending = 0;

            let old = core::mem::replace(&mut process.user_space, new_user_space);
            (old, closed)
        }; 
        /* 线程级 pending 也要清（exec 要求单线程，所以只有当前这一个）。 */
        manager.thread_mut(current_tid).sig_pending = 0;


        for file in &closed_files {
            manager.release_file(file);
        }

        *manager.thread_mut(current_tid).trap_cx_mut() = new_trap_cx;

        let root = manager.process(current_pid).user_space.root_ppn();

        drop(old_space);

        root
    };

        crate::mm::activate_page_table(new_root);

        // 施加 setuid/setgid 位(成功 exec 后才改变有效凭证)
        if let Some((u, ue, g, ge)) = elevate {
            let (cu, cue, cg, cge) = crate::task::current_creds();
            if ue != cue || ge != cge {
                crate::task::set_current_creds(u, ue, g, ge);
                log::info!("[exec] setuid/setgid applied: euid={}, egid={}", ue, ge);
            }
        }

        argc as isize
}

fn read_exec_args(
    args_ptr: usize,
    default_argv0: &str,
) -> Result<Vec<Vec<u8>>, isize> {
    if args_ptr == 0 {
        let mut argv = Vec::new();
        argv.push(default_argv0.as_bytes().to_vec());
        return Ok(argv);
    }

    let bytes = crate::task::read_current_user_bytes(
        args_ptr,
        core::mem::size_of::<UserExecArgs>(),
    )
    .ok_or(crate::syscall::errno::EFAULT)?;

    let raw = unsafe {
        core::ptr::read_unaligned(bytes.as_ptr() as *const UserExecArgs)
    };

    if raw.argc == 0 || raw.argc > EXEC_MAX_ARGS {
        log::info!("[exec] bad argc={} (limit {})", raw.argc, EXEC_MAX_ARGS);
        return Err(crate::syscall::errno::E2BIG);
    }

    let mut argv = Vec::new();

    for i in 0..raw.argc {
        let arg = raw.argv[i];

        if arg.ptr == 0 || arg.len > 256 {
            log::info!(
                "[exec] bad argv[{}]: ptr={:#x} len={}",
                i,
                arg.ptr,
                arg.len,
            );
            return Err(crate::syscall::errno::EINVAL);
        }

        let mut data = crate::task::read_current_user_bytes(arg.ptr, arg.len)
            .ok_or(crate::syscall::errno::EFAULT)?;

        if let Some(pos) = data.iter().position(|&c| c == 0) {
            data.truncate(pos);
        }

        argv.push(data);
    }

    Ok(argv)
}

fn write_to_user_space(
    user_space: &crate::mm::MemorySet,
    user_va: usize,
    data: &[u8],
) -> Option<()> {
    for (offset, byte) in data.iter().enumerate() {
        let va = user_va.checked_add(offset)?;
        let vpn = VirtAddr(va).floor();
        let page_offset = va & (PAGE_SIZE - 1);

        let pte = user_space.translate(vpn)?;
        let pa = (pte.ppn().0 << PAGE_SIZE_BITS) + page_offset;
        let kva = crate::mm::kernel_phys_to_virt(pa);

        unsafe {
            core::ptr::write_volatile(kva as *mut u8, *byte);
        }
    }

    Some(())
}

fn align_down(value: usize, align: usize) -> usize {
    value & !(align - 1)
}

fn push_usize_to_user_stack(
    user_space: &crate::mm::MemorySet,
    sp: &mut usize,
    value: usize,
) -> Option<()> {
    *sp -= core::mem::size_of::<usize>();
    write_to_user_space(user_space, *sp, &value.to_ne_bytes())
}

fn build_user_stack_with_args(
    user_space: &crate::mm::MemorySet,
    user_sp: usize,
    argv: &[Vec<u8>],
    env: &[Vec<u8>],
) -> Option<(usize, usize)> {
    let mut sp = user_sp;
    let mut arg_ptrs: Vec<usize> = Vec::new();
    let mut env_ptrs: Vec<usize> = Vec::new();

    // 字符串落到栈底（低地址）。顺序无所谓，关键是 argv[] 紧跟其 NULL 终止符、
    // 之后是 envp[] 与其 NULL 终止符，使 envp = argv_ptr + (argc + 1) * sizeof(char*)。
    for arg in argv.iter().rev() {
        sp -= arg.len() + 1;

        write_to_user_space(user_space, sp, arg)?;
        write_to_user_space(user_space, sp + arg.len(), &[0])?;

        arg_ptrs.push(sp);
    }

    for e in env.iter().rev() {
        sp -= e.len() + 1;

        write_to_user_space(user_space, sp, e)?;
        write_to_user_space(user_space, sp + e.len(), &[0])?;

        env_ptrs.push(sp);
    }

    arg_ptrs.reverse();
    env_ptrs.reverse();

    sp = align_down(sp, 16);

    // envp[] 以 NULL 结尾
    push_usize_to_user_stack(user_space, &mut sp, 0)?;
    for &ptr in env_ptrs.iter().rev() {
        push_usize_to_user_stack(user_space, &mut sp, ptr)?;
    }

    // argv[] 以 NULL 结尾
    push_usize_to_user_stack(user_space, &mut sp, 0)?;
    for &ptr in arg_ptrs.iter().rev() {
        push_usize_to_user_stack(user_space, &mut sp, ptr)?;
    }

    let argv_ptr = sp;

    Some((sp, argv_ptr))
}

pub fn current_file(fd: usize) -> Option<crate::fs::FileRef> {
    let pid = current_pid();
    let manager = lock_detect!(TASK_MANAGER);
    manager.get_file(pid, fd)
}

pub fn alloc_fd_current(file: crate::fs::FileRef) -> isize {
    let pid = current_pid();
    let mut manager = lock_detect!(TASK_MANAGER);
    manager.alloc_fd(pid, file)
}

pub fn close_fd_current(fd: usize) -> isize {
    let pid = current_pid();
    let mut manager = lock_detect!(TASK_MANAGER);
    manager.close_fd(pid, fd)
}

/// 给刚分配好的 fd 打上/清掉 FD_CLOEXEC。
pub fn set_fd_cloexec_current(fd: usize, on: bool) {
    let pid = current_pid();
    let mut manager = lock_detect!(TASK_MANAGER);
    let process = manager.process_mut(pid);
    if fd >= process.fd_cloexec.len() {
        process.fd_cloexec.resize(fd + 1, false);
    }
    process.fd_cloexec[fd] = on;
}

/// 记录 fd 的文件状态标志（访问模式 + O_APPEND + O_NONBLOCK）。
///
/// 必须在 open 时调一次：`F_GETFL` 的值就来自这里。少了这一步，
/// `fcntl(fd, F_GETFL) & O_ACCMODE` 永远报 O_RDONLY —— 用读模式打开的
/// 文件看起来像可写、反之亦然，属于"不崩但语义全错"。
pub fn set_fd_flags_current(fd: usize, flags: usize) {
    let pid = current_pid();
    let mut manager = lock_detect!(TASK_MANAGER);
    let process = manager.process_mut(pid);
    if fd >= process.fd_flags.len() {
        process.fd_flags.resize(fd + 1, 0);
    }
    process.fd_flags[fd] = flags & crate::fs::F_GETFL_MASK;
}

pub fn get_fd_flags_current(fd: usize) -> usize{
    let current_pid = current_pid();
    let mut manager = lock_detect!(TASK_MANAGER);
    let process = manager.process_mut(current_pid);
    process.fd_flags.get(fd).copied().unwrap_or(0)
}

/// fcntl(fd, cmd, arg)。
///
/// 支持的命令（其余返回 `-EINVAL`，不是 `-1` —— 后者会被 libc 当成 EIO）：
///   F_DUPFD / F_DUPFD_CLOEXEC —— 复制 fd 到「≥ arg 的最小空闲槽」
///   F_GETFD / F_SETFD          —— 描述符标志（只有 FD_CLOEXEC 一位）
///   F_GETFL / F_SETFL          —— 文件状态标志（访问模式 + O_APPEND + O_NONBLOCK）
pub fn set_fcntl(fd: usize, cmd: usize, arg: usize) -> isize {
    let pid = current_pid();
    let mut manager = lock_detect!(TASK_MANAGER);

    if fd >= manager.process(pid).fd_table.len()
        || manager.process(pid).fd_table[fd].is_none()
    {
        return -crate::syscall::errno::EBADF;
    }

    /* F_DUPFD 要动 fd 表本身（可能增长），必须先把上一段的 &Process 借还回去。 */
    if cmd == crate::fs::F_DUPFD || cmd == crate::fs::F_DUPFD_CLOEXEC {
        if arg >= MAX_FD_PER_PROCESS {
            return -crate::syscall::errno::EINVAL;
        }

        let file = match &manager.process(pid).fd_table[fd] {
            Some(f) => f.clone(),
            None => return -crate::syscall::errno::EBADF,
        };

        /* 管道要记账（reader/writer_count），和 dup/dup2 一样。 */
        file.on_fork();
        let new_fd = manager.alloc_fd_from(pid, file, arg);
        if new_fd < 0 {
            return -crate::syscall::errno::EMFILE;
        }
        manager.copy_fd_flags(pid, fd, new_fd as usize);

        /* F_DUPFD 的新 fd 不带 FD_CLOEXEC，F_DUPFD_CLOEXEC 才带。 */
        if cmd == crate::fs::F_DUPFD_CLOEXEC {
            let process = manager.process_mut(pid);
            let n = new_fd as usize;
            if n >= process.fd_cloexec.len() {
                process.fd_cloexec.resize(n + 1, false);
            }
            process.fd_cloexec[n] = true;
        }
        return new_fd;
    }

    let process = manager.process_mut(pid);
    match cmd {
        crate::fs::F_GETFL => process.fd_flags.get(fd).copied().unwrap_or(0) as isize,
        crate::fs::F_SETFL => {
            if fd >= process.fd_flags.len() {
                process.fd_flags.resize(fd + 1, 0);
            }
            /* 只改 O_APPEND / O_NONBLOCK，保留访问模式。
             * 直接 `= arg` 会让 `fcntl(fd, F_SETFL, O_NONBLOCK)` 把 O_RDWR
             * 悄悄变成 O_RDONLY —— 之后 write 就 EBADF，很难联想到 fcntl。 */
            let cur = process.fd_flags[fd];
            process.fd_flags[fd] =
                (cur & !crate::fs::F_SETFL_MASK) | (arg & crate::fs::F_SETFL_MASK);
            0
        }
        /* 描述符标志：只有一个 bit（FD_CLOEXEC）。git 在 fork 前用它标记
         * "这个 fd 不要漏给子进程"，exec 时由 close_non_standard_fds_on_exec 收。 */
        crate::fs::F_GETFD => {
            if process.fd_cloexec.get(fd).copied().unwrap_or(false) {
                crate::fs::FD_CLOEXEC as isize
            } else {
                0
            }
        }
        crate::fs::F_SETFD => {
            if fd >= process.fd_cloexec.len() {
                process.fd_cloexec.resize(fd + 1, false);
            }
            process.fd_cloexec[fd] = (arg & crate::fs::FD_CLOEXEC) != 0;
            0
        }
        _ => -crate::syscall::errno::EINVAL,
    }
}

pub fn current_cwd() -> String {
    let pid = current_pid();
    let manager = lock_detect!(TASK_MANAGER);
    manager.process(pid).cwd.clone()
}

pub fn set_current_cwd(new_cwd: String) -> isize {
    let pid = current_pid();
    let mut manager = lock_detect!(TASK_MANAGER);
    manager.process_mut(pid).cwd = new_cwd;
    0
}

/// 当前进程的文件创建掩码。新建文件/目录时按 `mode & !umask` 收窄。
///
/// boot 阶段无 current task（见 `current_creds`），用默认的 022：
/// 此时创建的是 `/data` 的骨架目录，权限本来就写死 0755。
pub fn current_umask() -> u16 {
    if processor::current_tid_opt().is_none() {
        return 0o022;
    }
    let pid = current_pid();
    let manager = lock_detect!(TASK_MANAGER);
    manager.process(pid).umask
}

/// 设置 umask，返回旧值（POSIX umask(2) 就是这么约定的：
/// 调用方常常写 `old = umask(0); umask(old);` 来"读一下当前掩码"）。
pub fn set_current_umask(mask: u16) -> u16 {
    let pid = current_pid();
    let mut manager = lock_detect!(TASK_MANAGER);
    let p = manager.process_mut(pid);
    let old = p.umask;
    p.umask = mask & 0o777;
    old
}

// ===== 环境变量子系统（内核态 PCB 存储，用户态经下方封装访问）=====

/// 取变量值（已克隆，调用方需先持有 key 的 &str，且不持 TASK_MANAGER 锁）。
pub fn env_get_current_value(key: &str) -> Option<String> {
    let pid = current_pid();
    let manager = lock_detect!(TASK_MANAGER);
    manager.process(pid).env_get(key).cloned()
}

/// 设置变量；key 为空返回 -1，否则返回 0。
pub fn env_set_current_value(key: &str, val: &str, overwrite: bool) -> isize {
    if key.is_empty() {
        return -1;
    }
    let pid = current_pid();
    let mut manager = lock_detect!(TASK_MANAGER);
    manager
        .process_mut(pid)
        .env_set(key.to_string(), val.to_string(), overwrite);
    0
}

/// 删除变量（不存在也返回 0）。
pub fn env_unset_current_value(key: &str) -> isize {
    let pid = current_pid();
    let mut manager = lock_detect!(TASK_MANAGER);
    manager.process_mut(pid).env_unset(key);
    0
}

/// 清空全部变量。
pub fn env_clear_current_values() -> isize {
    let pid = current_pid();
    let mut manager = lock_detect!(TASK_MANAGER);
    manager.process_mut(pid).env_clear();
    0
}

/// 复制整张环境表（供用户态 env 命令枚举）。
pub fn env_get_all_current() -> Vec<(String, String)> {
    let pid = current_pid();
    let manager = lock_detect!(TASK_MANAGER);
    manager.process(pid).env_pairs()
}

pub fn exit_current_and_run_next(exit_code: i32) -> ! {
    let current_tid = processor::current_tid();

    let task_cx_ptr = {
        let mut manager = lock_detect!(TASK_MANAGER);

        let current_pid = manager.pid_of_tid(current_tid);
        let tids = manager.process(current_pid).threads.clone();

        log::info!(
            "[task] process exit: pid={} current_tid={} code={}",
            current_pid,
            current_tid,
            exit_code,
        );

        manager.process_mut(current_pid).exit_code = exit_code;

        // 前台进程组退出 → 前台交还父进程所在的组（Ctrl+C 投递目标回退）
        //
        // 只有"整组都没人活着"才交还：管线 `a | b` 里 a 先退出时，前台必须
        // 仍然指着这个组，否则 b 还在跑、Ctrl+C 却打到了 shell 上。
        let exiting_pgid = manager.process(current_pid).pgid;
        if FRONT_PGID.load(Ordering::Relaxed) == exiting_pgid {
            let group_still_alive = manager
                .pids_in_group(exiting_pgid)
                .into_iter()
                .any(|p| p != current_pid && !manager.process_is_zombie(p));
            if !group_still_alive {
                if let Some(parent) = manager.process(current_pid).parent {
                    let parent_pgid = manager.process(parent).pgid;
                    FRONT_PGID.store(parent_pgid, Ordering::Relaxed);
                }
            }
        }

        // SIGCHLD: 子进程退出, 通知父进程(默认忽略, 父进程 waitpid 收尸)
        if let Some(parent) = manager.process(current_pid).parent {
            if let Some(p) = manager.try_process_mut(parent) {
                p.sig_pending |= 1u64 << super::SIGCHLD;
            }
        }

        // runnable_count 记账:把 Ready/Running -> Zombie/Dead 的线程数统计出来
        let mut runnable_dec = 0usize;
        for tid in tids {
            if tid == current_tid {
                let thread = manager.thread_mut(tid);
                if matches!(
                    thread.status,
                    ThreadStatus::Ready | ThreadStatus::Running
                ) {
                    runnable_dec += 1;
                }
                thread.status = ThreadStatus::Zombie;
                thread.block_reason = BlockReason::None;
                thread.exit_code = exit_code;
            } else if let Some(thread) = manager.try_thread_mut(tid) {
                if matches!(
                    thread.status,
                    ThreadStatus::Ready | ThreadStatus::Running
                ) {
                    runnable_dec += 1;
                }
                thread.status = ThreadStatus::Dead;
                thread.block_reason = BlockReason::None;
                thread.exit_code = exit_code;
            }
        }
        if runnable_dec > 0 {
            let process = manager.process_mut(current_pid);
            process.runnable_count = process.runnable_count.saturating_sub(runnable_dec);
        }

        let files: Vec<crate::fs::file::FileRef> = {
            let process = manager.process_mut(current_pid);
            let mut v = Vec::new();
            for slot in process.fd_table.iter_mut() {
                if let Some(file) = slot.take() {
                    v.push(file);
                }
            }
            v
        };

        for file in &files {
            manager.release_file(file);
        }

        //manager.wake_parent_waiting_for(current_pid);

        manager.thread_cx_ptr(current_tid)
    };

    let idle_cx_ptr = processor::idle_task_cx_ptr();
     

    // 新增：进程退出可能唤醒父进程的 waitpid
    //ipi::send_ipi_to_others(ipi::IpiKind::Reschedule, 0);

    unsafe {
        __switch(task_cx_ptr, idle_cx_ptr);
    }

    panic!("process returned after exit");
}

pub fn thread_create_current(
    entry: usize,
    arg0: usize,
    arg1: usize,
    user_stack_top: usize,
) -> isize {
    let ret = {
        let mut manager = lock_detect!(TASK_MANAGER);
        manager.create_thread_current(entry, arg0, arg1, user_stack_top)
    };
    // 新增：新线程就绪
    ipi::send_ipi_to_others(ipi::IpiKind::Reschedule, 0);
    ret
}

pub fn thread_exit_current(exit_code: i32) -> ! {
    let current_tid = processor::current_tid();

    let task_cx_ptr = {
        let mut manager = lock_detect!(TASK_MANAGER);

        let pid = manager.pid_of_tid(current_tid);

        log::info!(
            "[thread] exit: pid={} tid={} code={}",
            pid,
            current_tid,
            exit_code,
        );

        let was_runnable = {
            let thread = manager.thread_mut(current_tid);
            let r = matches!(
                thread.status,
                ThreadStatus::Ready | ThreadStatus::Running
            );
            thread.status = ThreadStatus::Zombie;
            thread.block_reason = BlockReason::None;
            thread.exit_code = exit_code;
            r
        };
        if was_runnable {
            let process = manager.process_mut(pid);
            process.runnable_count = process.runnable_count.saturating_sub(1);
        }

        //manager.wake_threads_joining(current_tid);

        manager.thread_cx_ptr(current_tid)
    };

    let idle_cx_ptr = processor::idle_task_cx_ptr();
     

    // 新增：退出并唤醒 joiners
    //ipi::send_ipi_to_others(ipi::IpiKind::Reschedule, 0);

    unsafe {
        __switch(task_cx_ptr, idle_cx_ptr);
    }

    panic!("zombie thread returned after thread_exit");
}

pub fn thread_join_current(target_tid: Tid, exit_code_ptr: usize) -> isize {
    let current_tid = processor::current_tid();

    if target_tid == current_tid {
        return -1;
    }

    loop {
        let task_cx_ptr = {
            let mut manager = lock_detect!(TASK_MANAGER);

            let current_pid = manager.pid_of_tid(current_tid);

            let target_state = {
                let Some(target) = manager.try_thread(target_tid) else {
                    return -1;
                };

                if target.pid != current_pid {
                    return -1;
                }

                (target.status, target.exit_code, target.running_on)
            };

            match target_state {
                (ThreadStatus::Zombie | ThreadStatus::Dead, code, running_on) => {
                    if running_on.is_some() {
                        manager.block_thread(
                            current_tid,
                            BlockReason::Join { tid: target_tid },
                        );
                        manager.thread_cx_ptr(current_tid)
                    } else {
                        if manager.write_user_i32(current_pid, exit_code_ptr, code).is_none() {
                            return -1;
                        }

                        manager.reap_thread(target_tid);
                        return target_tid as isize;
                    }
                }

                _ => {
                    manager.block_thread(
                        current_tid,
                        BlockReason::Join { tid: target_tid },
                    );
                    manager.thread_cx_ptr(current_tid)
                }
            }
        };

        let idle_cx_ptr = processor::idle_task_cx_ptr();
         
        // join 阻塞自己，被唤醒时由目标线程的退出负责发 IPI，此处无需
        unsafe {
            __switch(task_cx_ptr, idle_cx_ptr);
        }
    }
}


fn mmap_prot_to_perm(prot: usize) -> Option<crate::mm::MapPermission> {
    use crate::mm::MapPermission;

    if prot & !(crate::task::process::PROT_READ
        | crate::task::process::PROT_WRITE
        | crate::task::process::PROT_EXEC) != 0
    {
        return None;
    }

    if prot == 0 {
        return None;
    }

    let mut perm = MapPermission::U;

    if prot & crate::task::process::PROT_READ != 0 {
        perm = perm.union(MapPermission::R);
    }

    if prot & crate::task::process::PROT_WRITE != 0 {
        perm = perm.union(MapPermission::W);
    }

    if prot & crate::task::process::PROT_EXEC != 0 {
        perm = perm.union(MapPermission::X);
    }

    Some(perm)
}


/// mprotect(addr, len, prot): 修改已有映射的权限(TCC tccrun 的 JIT 需要)。
pub fn mprotect_current(addr: usize, len: usize, prot: usize) -> isize {
    let perm = match mmap_prot_to_perm(prot) {
        Some(perm) => perm,
        None => return -1,
    };
    if len == 0 {
        return -1;
    }
    let start = crate::mm::align_down(addr, crate::mm::config::PAGE_SIZE);
    let end = crate::mm::align_up(
        addr.checked_add(len).unwrap_or(usize::MAX),
        crate::mm::config::PAGE_SIZE,
    );
    let mut manager = lock_detect!(TASK_MANAGER);
    let pid = manager.current_pid();
    let process = manager.process_mut(pid);
    if process.user_space.change_permission(
        crate::mm::VirtAddr(start),
        crate::mm::VirtAddr(end),
        perm,
    ) {
        0
    } else {
        -1
    }
}

pub fn mmap_current(len: usize, prot: usize) -> isize {
    let perm = match mmap_prot_to_perm(prot) {
        Some(perm) => perm,
        None => return -1,
    };

    let (pid, start, end) = {
        let mut manager = lock_detect!(TASK_MANAGER);
        let pid = manager.current_pid();

        let (start, end) = {
            let process = manager.process_mut(pid);

            match process.alloc_mmap_range(len) {
                Some(range) => range,
                None => return -1,
            }
        };

        {
            let process = manager.process_mut(pid);

            process.user_space.insert_area(crate::mm::MapArea::new(
                crate::mm::VirtAddr(start),
                crate::mm::VirtAddr(end),
                crate::mm::MapType::Framed,
                perm,
            ));

            let start_vpn = crate::mm::VirtAddr(start).floor();
            let end_vpn = crate::mm::VirtAddr(end).ceil();

            for vpn_id in start_vpn.0..end_vpn.0 {
                let vpn = crate::mm::VirtPageNum(vpn_id);

                if process.user_space.translate(vpn).is_none() {
                    log::error!(
                        "[mmap] verify failed: pid={} start={:#x} end={:#x} missing vpn={:?}",
                        pid,
                        start,
                        end,
                        vpn,
                    );

                    return -1;
                }
            }

            process.mmap_areas.push(crate::task::process::MmapArea {
                start,
                end,
                prot,
            });
        }

        (pid, start, end)
    }; // 关键：这里释放 TASK_MANAGER

    /*
     * mmap 是新增映射，先不要在持 TASK_MANAGER 时等远程 ACK。
     * 调试阶段可以只做本核 flush，确认是否解决卡死。
     */
    crate::arch::flush_tlb();

    /*
     * 如果本核 flush 后稳定，再考虑换成异步广播：
     * crate::arch::tlb_shootdown_broadcast();
     *
     * 暂时不要在这里用 tlb_shootdown_sync()，先验证是否就是 ACK 等待卡死。
     */

    log::info!(
        "[mmap] pid={} len={} prot={:#x} => {:#x}..{:#x}",
        pid,
        len,
        prot,
        start,
        end,
    );

    start as isize
}

pub fn munmap_current(addr: usize, len: usize) -> isize {
    if addr == 0 || len == 0 {
        return -1;
    }

    let start = crate::mm::align_down(addr, crate::mm::config::PAGE_SIZE);
    let len = crate::mm::align_up(len, crate::mm::config::PAGE_SIZE);

    let end = match start.checked_add(len) {
        Some(end) => end,
        None => return -1,
    };

    let mut manager = lock_detect!(TASK_MANAGER);
    let pid = manager.current_pid();

    {
        let process = manager.process_mut(pid);

        let Some(index) = process
            .mmap_areas
            .iter()
            .position(|area| area.start == start && area.end == end)
        else {
            return -1;
        };

        process.mmap_areas.remove(index);

        if !process.user_space.remove_area(
            crate::mm::VirtAddr(start),
            crate::mm::VirtAddr(end),
        ) {
            return -1;
        }
        process.dealloc_mmap_range(start, end);
    }

    crate::arch::tlb_shootdown_sync();

    log::info!(
        "[munmap] pid={} {:#x}..{:#x}",
        pid,
        start,
        end,
    );

    0
}


pub fn set_thread_tickets_current(tid: usize, tickets: usize) -> isize {
    if tickets == 0 {
        return -1;
    }

    let mut manager = lock_detect!(TASK_MANAGER);
    let current_tid = processor::current_tid();
    let current_pid = manager.pid_of_tid(current_tid);

    let Some(thread) = manager.try_thread(tid) else {
        return -1;
    };

    if thread.pid != current_pid {
        return -1;
    }

    let thread = manager.thread_mut(tid);
    thread.tickets = tickets;
    thread.stride = crate::task::process::stride_from_tickets(tickets);

    0
}

pub fn set_process_tickets_current(pid: usize, tickets: usize) -> isize {
    if tickets == 0 {
        return -1;
    }

    let mut manager = lock_detect!(TASK_MANAGER);

    if manager.try_process(pid).is_none() {
        return -1;
    }

    let process = manager.process_mut(pid);
    process.tickets = tickets;
    process.effective_tickets = tickets;
    process.stride = crate::task::process::stride_from_tickets(tickets);

    0
}

pub fn set_my_tickets_current(tickets: usize) -> isize {
    if tickets == 0 {
        return -1;
    }

    let mut manager = lock_detect!(TASK_MANAGER);
    let tid = processor::current_tid();
    let pid = manager.pid_of_tid(tid);


    if manager.try_process(pid).is_none() {
        return -1;
    }

    let process = manager.process_mut(pid);
    process.tickets = tickets;
    process.effective_tickets = tickets;
    process.stride = crate::task::process::stride_from_tickets(tickets);

    0
}


pub fn get_thread_tickets_current(tid: usize) -> isize {
    let mut manager = lock_detect!(TASK_MANAGER);
    let current_tid = processor::current_tid();
    let current_pid = manager.pid_of_tid(current_tid);

    let Some(thread) = manager.try_thread(tid) else {
        return -1;
    };

    if thread.pid != current_pid {
        return -1;
    }

    let thread = manager.thread_mut(tid);
    thread.tickets as isize
}

pub fn get_process_tickets_current(pid: usize) -> isize {
    let mut manager = lock_detect!(TASK_MANAGER);

    if manager.try_process(pid).is_none() {
        return -1;
    }

    let process = manager.process_mut(pid);
    process.tickets as isize
}

pub fn get_my_tickets_current() -> isize {
    let mut manager = lock_detect!(TASK_MANAGER);
    let tid = processor::current_tid();
    let pid = manager.pid_of_tid(tid);

    if manager.try_process(pid).is_none() {
        return -1;
    }

    let process = manager.process_mut(pid);
    process.tickets as isize
}

pub fn set_sched_alpha_current(alpha: isize) -> isize {
    if alpha < 0 || alpha > 100 {
        return -1;
    }

    let mut manager = lock_detect!(TASK_MANAGER);
    manager.set_sched_alpha(alpha)
}

pub fn get_sched_alpha_current() -> isize {
    let manager = lock_detect!(TASK_MANAGER);
    manager.get_sched_alpha() as isize
}

pub fn account_current_tick() {
    let Some(tid) = processor::current_tid_opt() else { return };

    let mut manager = match TASK_MANAGER.try_lock() {
        Some(g) => g,
        None => {
            return;
        }
    };

    let pid = manager.pid_of_tid(tid);
    manager.thread_mut(tid).run_ticks += 1;
    manager.process_mut(pid).run_ticks += 1;
}

fn write_value_to_user<T: Copy>(user_ptr: usize, value: &T) -> isize {
    if user_ptr == 0 {
        return -1;
    }

    let bytes = unsafe {
        core::slice::from_raw_parts(
            value as *const T as *const u8,
            core::mem::size_of::<T>(),
        )
    };

    match crate::task::write_current_user_bytes(user_ptr, bytes) {
        Some(n) if n == bytes.len() => 0,
        _ => -1,
    }
}


#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct SchedProcStat {
    pub pid: i32,
    pub tickets: i32,
    pub effective_tickets: i32,
    pub ready_threads: i32,
    pub alpha: i32,

    pub run_ticks: usize,
    pub pass: usize,
    pub stride: usize,
}

pub fn get_process_sched_stat(pid: usize, stat_ptr: usize) -> isize {
    if stat_ptr == 0 {
        return -1;
    }

    let stat = {
        let manager = lock_detect!(TASK_MANAGER);

        if manager.try_process(pid).is_none() {
            return -1;
        }

        let process = manager.process(pid);

        // runnable_count 缓存漂移自检(仅 syscall 路径,代价可忽略):
        // 若告警出现,说明某个状态变迁点漏记了账,请把 pid 和现场贴出来。
        {
            let cached = process.runnable_count;
            let actual = manager.count_runnable_threads_in_process(pid);
            if cached != actual {
                log::warn!(
                    "[sched] runnable_count drift: pid={} cached={} actual={}",
                    pid,
                    cached,
                    actual,
                );
            }
        }

        let runnable_threads = manager
            .count_alive_threads_in_process(pid)
            .max(1);

        let alpha = manager.get_sched_alpha();
        let factor = super::math::sched_thread_scale(runnable_threads, alpha);

        let tickets = process.tickets.max(1);

        let effective_tickets = tickets
            .saturating_mul(factor)
            .max(1);

        let stride =
            crate::task::process::stride_from_tickets(effective_tickets);

//         log::warn!(
//     "[stat] pid={} threads.len={} runnable={} alpha={} factor={} tickets={} eff={}",
//     pid,
//     process.threads.len(),
//     runnable_threads,
//     alpha,
//     factor,
//     tickets,
//     effective_tickets,
// );
    

        SchedProcStat {
            pid: pid as i32,
            tickets: tickets as i32,
            effective_tickets: effective_tickets as i32,
            ready_threads: runnable_threads as i32,
            alpha: alpha as i32,

            run_ticks: process.run_ticks,
            pass: process.pass,
            stride,
        }
    };

    write_value_to_user(stat_ptr, &stat)
}

pub fn reset_sched_stat() -> isize {
    let mut manager = lock_detect!(TASK_MANAGER);
    manager.reset_sched_stat()
}

pub fn new_pipe(fd : usize) -> isize {
    let file = crate::fs::pipe::make_pipe();
    let pipe_fd = (crate::task::alloc_fd_current(file.0),crate::task::alloc_fd_current(file.1));
    if pipe_fd.0 == 0 || pipe_fd.1 == -1{
        -1
    }
    else{
        let read_fd_i32 = pipe_fd.0 as i32;
        let write_fd_i32 = pipe_fd.1 as i32;

        let r0 = write_value_to_user(fd, &read_fd_i32);
        let r1 = write_value_to_user(fd + core::mem::size_of::<i32>(), &write_fd_i32);

        if r0 == -1 || r1 == -1 {
            return -1;
        }
        0
    }
}

pub fn block_current_on_pipe_read() -> isize {
    let current_tid = processor::current_tid();

    let task_cx_ptr = {
        let mut manager = lock_detect!(TASK_MANAGER);

        log::info!(
            "[task] thread {} is blocked in pipe read",
            current_tid,
        );

        manager.block_thread(
            current_tid,
            BlockReason::PipeRead,
        );

        manager.thread_cx_ptr(current_tid)
    };

    let idle_cx_ptr = processor::idle_task_cx_ptr();
     
    // 阻塞自己，不需要 IPI
    unsafe {
        __switch(task_cx_ptr, idle_cx_ptr);
    }

    0
}

pub fn block_current_on_pipe_write() -> isize {
    let current_tid = processor::current_tid();

    let task_cx_ptr = {
        let mut manager = lock_detect!(TASK_MANAGER);

        log::info!(
            "[task] thread {} is blocked in pipe write",
            current_tid,
        );

        manager.block_thread(
            current_tid,
            BlockReason::PipeWrite,
        );

        manager.thread_cx_ptr(current_tid)
    };

    let idle_cx_ptr = processor::idle_task_cx_ptr();
     
    // 阻塞自己
    unsafe {
        __switch(task_cx_ptr, idle_cx_ptr);
    }

    0
}

pub fn wake_pipe_readers() {
    {
        let mut manager = lock_detect!(TASK_MANAGER);
        manager.wake_threads_by_reason(BlockReason::PipeRead);
    }
    // 新增：唤醒了一组 pipe 读者
    ipi::send_ipi_to_others(ipi::IpiKind::Reschedule, 0);
}

pub fn wake_pipe_writers() {
    {
        let mut manager = lock_detect!(TASK_MANAGER);
        manager.wake_threads_by_reason(BlockReason::PipeWrite);
    }
    // 新增
    ipi::send_ipi_to_others(ipi::IpiKind::Reschedule, 0);
}

/// dup2/dup3 的 fd 表上限。
const MAX_FD_PER_PROCESS: usize = 1024;

pub fn dup2(old_fd : usize,new_fd : usize) -> isize{
    let mut manager = lock_detect!(TASK_MANAGER);
    let current_tid = processor::current_tid();
    let current_pid = manager.pid_of_tid(current_tid);
    
    if old_fd == new_fd {
        return new_fd as isize;
    }
    if new_fd >= MAX_FD_PER_PROCESS {
        return -1;
    }
    
    let old_file = {
        let process = manager.process_mut(current_pid);
        if old_fd >= process.fd_table.len(){
            return -1;
        }
        match &process.fd_table[old_fd]{
            Some(f) => f.clone(),
            None => return -1,
        }
    };

        
    let new_file = {
        let process = manager.process_mut(current_pid);
        if new_fd >= process.fd_table.len(){
            // 增长到 new_fd+1。中间新增的空槽必须登记进 free_fds，
            // 否则 alloc_fd 的两条路径（pop free_fds / push 到表尾）都看不到它们，
            // 这些 fd 就永久泄漏了。new_fd 自己马上被占用，不进 free_fds。
            let old_len = process.fd_table.len();
            for _ in old_len..=new_fd {
                process.fd_table.push(None);
            }
            for i in old_len..new_fd {
                process.free_fds.push(i);
            }
            None
        } else {
            // 覆盖已关闭的低位 fd 时，必须把它从 free_fds 里摘掉：
            // 否则后续 open 会 pop 到它，而 alloc_fd 的 assert!(slot.is_none()) 会 panic。
            if let Some(pos) = process.free_fds.iter().position(|&x| x == new_fd) {
                process.free_fds.remove(pos);
            }
            process.fd_table[new_fd].take()
        }
    };
    
    if let Some(f) = &new_file {
        manager.release_file(f);
    }

    old_file.on_fork(); 
    {
        let process = manager.process_mut(current_pid);
        process.fd_table[new_fd] = Some(old_file);
        /* dup2 得到的新 fd 不带 FD_CLOEXEC（POSIX）。少了这一句，
         * `dup2(f, 1)` 之后 exec 会把 stdout 关掉。 */
        if new_fd < process.fd_cloexec.len() {
            process.fd_cloexec[new_fd] = false;
        }
    }

    /* 同一个 open file description，文件状态标志要一起搬过去。 */
    manager.copy_fd_flags(current_pid, old_fd, new_fd);

    new_fd as isize
}

pub fn dup(old_fd: usize) -> isize {
    let mut manager = lock_detect!(TASK_MANAGER);
    let current_tid = processor::current_tid();
    let current_pid = manager.pid_of_tid(current_tid);

    let old_file = {
        let process = manager.process_mut(current_pid);
        if old_fd >= process.fd_table.len() {
            return -1;
        }
        match &process.fd_table[old_fd] {
            Some(f) => f.clone(),
            None => return -1,
        }
    };

    {
        let process = manager.process(current_pid);
        if process.free_fds.is_empty() && process.fd_table.len() >= MAX_FD_PER_PROCESS {
            return -1;
        }
    }

    old_file.on_fork();
    let new_fd = {
        let process = manager.process_mut(current_pid);
        // 扫最低空闲槽；一个都没有就追加到末尾
        let mut target = usize::MAX;
        for (i, slot) in process.fd_table.iter().enumerate() {
            if slot.is_none() {
                target = i;
                break;
            }
        }
        if target == usize::MAX {
            process.fd_table.push(None);
            target = process.fd_table.len() - 1;
        } else if let Some(pos) = process.free_fds.iter().position(|&x| x == target) {
            // 摘掉 free_fds 里的同一项，保持两个结构一致
            process.free_fds.remove(pos);
        }
        process.fd_table[target] = Some(old_file);
        target as isize
    };

    /* 同一个 open file description，文件状态标志跟着走。 */
    manager.copy_fd_flags(current_pid, old_fd, new_fd as usize);
    new_fd
}

/// 前台进程组号（Ctrl+C / Ctrl+Z 的投递目标）。
///
/// 为什么是组而不是 pid：一条管线 `a | b | c` 是三个进程，按 Ctrl+C 时
/// 必须一起收到信号；只打一个 pid 会让另外两个变成孤儿继续跑（还会写死掉的
/// 管道）。真实终端设备有 `tcsetpgrp()` 干这件事，我们还没有终端对象，
/// 所以先用一个全局变量当"终端的前台组"——是一个进程的简化，语义已经对了。
///
/// 单命令时 pgid == pid（shell fork 后立刻 setpgid(child, child)），所以
/// 老代码传 pid 也照样能用。
static FRONT_PGID: AtomicUsize = AtomicUsize::new(1);

pub fn set_front(pgid: usize) {
    FRONT_PGID.store(pgid, core::sync::atomic::Ordering::Relaxed);
}

pub fn front_pgid() -> usize {
    FRONT_PGID.load(core::sync::atomic::Ordering::Relaxed)
}

/// Ctrl+C 投递: 向前台进程组发 SIGINT（组内不忽略的都会终止）。
pub fn kill_front_sigint() {
    let pgid = front_pgid();
    if pgid != 0 {
        let _ = kill(-(pgid as isize), super::SIGINT);
    }
}

/// Ctrl+Z 投递: 向前台进程组发 SIGTSTP（默认动作 = 停止，由 shell 的 `fg`/`bg` 继续）。
pub fn kill_front_tstp() {
    let pgid = front_pgid();
    if pgid != 0 {
        let _ = kill(-(pgid as isize), super::SIGTSTP);
    }
}

/// 查询进程组号。pid=0 表示当前进程；返回 -1 表示没有这个进程。
pub fn pgid_of(pid: isize) -> isize {
    let manager = lock_detect!(TASK_MANAGER);
    let target = if pid == 0 {
        manager.current_pid()
    } else if pid < 0 {
        return -1;
    } else {
        pid as usize
    };
    match manager.try_process(target) {
        Some(p) => p.pgid as isize,
        None => -crate::syscall::errno::ESRCH,
    }
}

/// 把 target 放进组 pgid（POSIX setpgid 的一个诚实子集）：
///
///   - 只能改自己，或自己的直接子进程（否则 EPERM）
///   - pid = 0 表示"我自己"；pgid = 0 表示"用 target 的 pid 当组号"，
///     也就是"自成一组、自己当组长"，这个总是允许（因为它必然是个空组号）
///   - 除此之外，目标组必须已经存在（本模型不创建空组，组是派生视图）
///
/// 没有 session 边界检查：POSIX 的"同一 session 内""session leader 不能改"
/// 依赖 session 的成员集合，而这里 sid 只是个标号（见 PCB::sid），
/// 无法用来判定"同会话"，所以明确省略而不是假装检查。
pub fn set_pgid(pid: isize, pgid: isize) -> isize {
    let mut manager = lock_detect!(TASK_MANAGER);
    let self_pid = manager.current_pid();

    let target = if pid == 0 {
        self_pid
    } else if pid < 0 {
        return -crate::syscall::errno::EINVAL;
    } else {
        pid as usize
    };

    // 1) 目标进程必须存在
    if manager.try_process(target).is_none() {
        return -crate::syscall::errno::ESRCH;
    }

    // 2) 只能改自己或直接子进程
    if target != self_pid {
        let is_child = manager
            .try_process(self_pid)
            .map(|p| p.children.contains(&target))
            .unwrap_or(false);
        if !is_child {
            return -crate::syscall::errno::EPERM;
        }
    }

    // 3) 组号：0 → 自己当组长；负数非法
    let new_pgid = if pgid == 0 {
        target
    } else if pgid < 0 {
        return -crate::syscall::errno::EINVAL;
    } else {
        pgid as usize
    };

    // 4) 除了"自成一组"，目标组必须已存在
    if new_pgid != target && !manager.group_exists(new_pgid) {
        return -crate::syscall::errno::EPERM;
    }

    manager.process_mut(target).pgid = new_pgid;
    0
}

/// setsid(): 新建会话，调用者成为会话首进程 + 新进程组组长，返回新 sid。
///
/// 三条 POSIX 语义里能做到的：
///   - 组长不能调（EPERM）—— 因为它已经是组长，再"新建组"就是自指，
///     POSIX 拿这条挡住 fork 后忘了再 fork 一步的进程
///   - 成功后 pgid = sid = pid
///
/// 做不到的一条（不假装做）：会话和控制终端绑定、脱离时给前台组发 SIGHUP。
/// 本内核没有终端对象（`tcsetpgrp` 也没做），所以 setsid 的效果就是
/// "自成一组 + 记下自己是会话首进程"，足够 daemonize 用 —— git 的
/// `daemonize()` 要的正是"别再收终端信号"，而这里终端信号本来就只发给
/// `FRONT_PGID`，新组自然不在其中。
pub fn setsid() -> isize {
    let mut manager = lock_detect!(TASK_MANAGER);
    let pid = manager.current_pid();

    if manager.process(pid).pgid == pid {
        return -crate::syscall::errno::EPERM;
    }

    let p = manager.process_mut(pid);
    p.pgid = pid;
    p.sid = pid;
    pid as isize
}

/// signal(sig, action): 设置信号处置。
///
///   action = 0 → SIG_DFL（跟内核默认动作）
///   action = 1 → SIG_IGN
///   action ≥ 2 → 用户态 handler 入口地址；`restorer` 必须是 sigreturn
///                trampoline 的地址（为 0 就拒绝：没有 trampoline 的 handler
///                进得去出不来，比装不上更难查）
///   action == SIG_QUERY → 只查不改，直接把旧处置返回
///                （libc 的 `sigaction(sig, NULL, &old)` 走这条）
///
/// SIGKILL / SIGSTOP 不可改（但可查）。返回旧处置（0 / 1 / handler 地址），失败 -1。
pub fn sig_set(sig: usize, action: usize, restorer: usize) -> isize {
    if sig >= super::NSIG {
        return -1;
    }
    let pid = current_pid();
    let mut manager = lock_detect!(TASK_MANAGER);
    let process = manager.process_mut(pid);

    /* 只查不改（`sigaction(sig, NULL, &old)`）。放在 unblockable 检查之前：
     * 查询 SIGKILL / SIGSTOP 的当前处置是合法的（POSIX 允许），只是改不了。
     * 返回编码与"改"完全一致：0=DFL、1=IGN、其他=handler 入口地址。 */
    if action == super::SIG_QUERY {
        return match process.sig_actions[sig] {
            SigAction::Default => 0,
            SigAction::Ignore => 1,
            SigAction::Handler { entry, .. } => entry as isize,
        };
    }

    if super::unblockable(sig) {
        return -1;
    }

    let old = process.sig_actions[sig];
    process.sig_actions[sig] = match action {
        0 => SigAction::Default,
        1 => SigAction::Ignore,
        entry => {
            if restorer == 0 {
                return -1;
            }
            SigAction::Handler { entry, restorer }
        }
    };

    match old {
        SigAction::Default => 0,
        SigAction::Ignore => 1,
        SigAction::Handler { entry, .. } => entry as isize,
    }
}

/// kill(pid, sig)：`pid > 0` 投给那个进程；`pid < 0` 投给 `|pid|` 号进程组；
/// `pid == 0` 投给调用者自己所在的组。返回 0 / -1。
///
/// 组投递是作业控制的地基：`a | b | c` 按 Ctrl+C 要一起收到，只打一个 pid
/// 会让另外两个继续跑并向死管道写。
pub fn kill(pid: isize, sig: usize) -> isize {
    if sig >= super::NSIG {
        return -1;
    }

    if pid > 0 {
        return kill_one(pid as usize, sig);
    }

    let pgid = if pid == 0 {
        let manager = lock_detect!(TASK_MANAGER);
        match manager.try_process(manager.current_pid()) {
            Some(p) => p.pgid,
            None => return -1,
        }
    } else {
        (-pid) as usize
    };

    /* 先把成员收集出来，再锁外逐个投递 —— kill_one 自己会加锁，
     * 持锁调用就是死锁（lock_detect 会直接 panic）。 */
    let targets = {
        let manager = lock_detect!(TASK_MANAGER);
        manager.pids_in_group(pgid)
    };

    if targets.is_empty() {
        return -1;      // 组不存在 = ESRCH
    }

    let mut ret = 0;
    for t in targets {
        if kill_one(t, sig) < 0 {
            ret = -1;   // 个别失败不影响其余成员收到信号
        }
    }
    ret
}

/// 投给单个进程（`kill(pid>0)` 与组投递的共同实现）。
fn kill_one(pid: usize, sig: usize) -> isize {
    if sig >= super::NSIG {
        return -1;
    }

    let mut need_ipi = false;

    {
        let mut manager = lock_detect!(TASK_MANAGER);

        if manager.try_process(pid).is_none() {
            return -1;
        }

        /* 被停住的进程走不到 trap 出口，pending 里的信号永远没人处理。
         * POSIX 只给两个信号开了豁免： */
        let stopped = manager.process_is_stopped(pid);

        if stopped && sig == super::SIGKILL {
            /* 先试"就地结束"：停住的进程没有线程在跑，所以能做完整退出簿记。
             * 做不了（有线程还在 hart 上）就退回普通路径，让它自己处理。 */
            if manager.force_exit_stopped(pid, 128 + sig as i32, false, sig) {
                if let Some(parent) = manager.process(pid).parent {
                    if let Some(p) = manager.try_process_mut(parent) {
                        p.sig_pending |= 1u64 << super::SIGCHLD;
                    }
                    if manager.wake_parent_waiting_for(pid) {
                        need_ipi = true;
                    }
                }
                let killed_pgid = manager.process(pid).pgid;
                if FRONT_PGID.load(Ordering::Relaxed) == killed_pgid {
                    /* 和退出路径同样的判断：组里还有活着的成员就别抢回前台 */
                    let group_still_alive = manager
                        .pids_in_group(killed_pgid)
                        .into_iter()
                        .any(|p| p != pid && !manager.process_is_zombie(p));
                    if !group_still_alive {
                        if let Some(parent) = manager.process(pid).parent {
                            let parent_pgid = manager.process(parent).pgid;
                            FRONT_PGID.store(parent_pgid, Ordering::Relaxed);
                        }
                    }
                }
                drop(manager);
                if need_ipi {
                    ipi::send_ipi_to_others(ipi::IpiKind::Reschedule, 0);
                }
                return 0;
            }
        }

        manager.process_mut(pid).sig_pending |= 1u64 << sig;

        /* SIGCONT 是唯一能把停止态拉回来的信号：立刻继续，别等它自己跑
         * （它跑不了 —— 停止态没有线程在调度队列里）。 */
        if stopped && sig == super::SIGCONT {
            if manager.continue_process(pid) {
                need_ipi = true;
            }
        }

        let tids = manager.process(pid).threads.clone();

        for tid in tids {
            /* SIGCONT 只是"解除暂停"：不要把阻塞中的线程也拽起来 ——
             * 那会让一次 pipe read / stdin read 收到假的 EINTR
             * （POSIX 里 SIGCONT 不打断慢系统调用）。已知缺口：给 SIGCONT
             * 装了 handler 的程序，在阻塞中不会被告知 —— 需要"逐线程判断
             * 该信号是否可忽略"才能做对，教学规模下不值得。 */
            if sig == super::SIGCONT
                && manager.thread(tid).status == ThreadStatus::Blocking
            {
                continue;
            }
            if manager.mark_thread_ready(tid) {
                need_ipi = true;
            }
        }
    }

    if need_ipi {
        ipi::send_ipi_to_others(ipi::IpiKind::Reschedule, 0);
    }

    0
}

/// sigpending(set_ptr)：把"被本线程屏蔽、且正挂着"的信号写回用户态 set。
///
/// 注意不是"所有 pending"：没被屏蔽的那些下一刻就会投递，告诉调用方也没用。
/// POSIX 的定义就是 `pending & blocked` —— 它回答的是"我该解除哪些屏蔽"。
/// 两级 pending 都要算上（同步异常挂在线程级，kill 挂在进程级）。
pub fn sigpending(set_ptr: usize) -> isize {
    if set_ptr == 0 { return -1; }
    let tid = processor::current_tid();
    let pid = current_pid();

    let mask = {
        let manager = lock_detect!(TASK_MANAGER);
        let t = match manager.try_thread(tid) { Some(t) => t, None => return -1 };
        let p = match manager.try_process(pid) { Some(p) => p, None => return -1 };
        (t.sig_pending | p.sig_pending) & t.sig_blocked
    };

    /* 内核内部 bit(sig) -> 用户布局 bit(sig-1)，与 sigprocmask 同一套换算 */
    let word = mask >> 1;
    let mut buf = [0u8; 8];
    for i in 0..8 { buf[i] = ((word >> (i * 8)) & 0xff) as u8; }
    match write_current_user_bytes(set_ptr, &buf) {
        Some(_) => 0,
        None => -1,
    }
}

/// tgkill(pid, tid, sig)：把信号投给指定线程（thread-directed）。
///
/// 和 `kill` 的区别只有一点：kill 挂在进程上，任意一个未屏蔽它的线程都可能接；
/// 这个只挂在该线程的 `sig_pending` 上，别人不会替它处理。
/// 要求 `pid` 与目标线程的 pid 一致（防止 tid 复用后打错进程，Linux 也这么要求）。
pub fn tgkill(pid: usize, tid: usize, sig: usize) -> isize {
    if sig >= super::NSIG {
        return -1;
    }
    let mut need_ipi = false;
    {
        let mut manager = lock_detect!(TASK_MANAGER);
        match manager.try_thread(tid) {
            Some(t) if t.pid == pid => {}
            _ => return -1,
        }
        if let Some(t) = manager.try_thread_mut(tid) {
            t.sig_pending |= 1u64 << sig;
        }
        if manager.mark_thread_ready(tid) {
            need_ipi = true;
        }
    }
    if need_ipi {
        ipi::send_ipi_to_others(ipi::IpiKind::Reschedule, 0);
    }
    0
}

/// 当前线程此刻是否有"可投递且会做事"的信号（未被本线程屏蔽的 pending）。
///
/// 阻塞型 syscall 靠它判断"我刚才是被信号唤醒的吗"—— 是就返回 EINTR，
/// 让用户态决定重试还是借机退出（POSIX 的默认行为；没实现 SA_RESTART）。
pub fn signal_pending_now() -> bool {
    let pid = current_pid();
    let tid = processor::current_tid();
    let manager = lock_detect!(TASK_MANAGER);
    manager.deliverable_pending(tid, pid)
}

/// 信号投递：在"返回用户态之前"调用（trap 出口），所以能拿到 `cx` 并改写它。
///
/// 三种处置：忽略（清位即可）、默认动作（杀/停/继续）、用户 handler
/// （把现场搬到用户栈，再把返回现场换成 handler 入口）。
/// 返回值：`true` 表示现场已被改写成 handler 入口（调用方不要再按"没处理"处理）；
/// `false` 表示信号被忽略或走了默认动作里"什么都不做"那一支。
/// 同步异常的调用点（SIGSEGV/SIGILL）必须看这个返回值：有 handler 时要放它去跑，
/// 否则就会把"该进 handler 的"当成"没人管"。
pub fn do_signal(cx: &mut TrapContext) -> bool {
    let current_pid = current_pid();
    let current_tid = processor::current_tid();
    let mut manager = lock_detect!(TASK_MANAGER);

    /* 三样东西各归其位：pending 两级合并、屏蔽字看本线程。
     * 屏蔽中的信号不清位、不丢 —— 解除屏蔽后下一次 trap 出口会补投。 */
    let blocked = manager.thread(current_tid).sig_blocked;
    let tpend = manager.thread(current_tid).sig_pending;
    let ppend = manager.process(current_pid).sig_pending;
    let pending = (tpend | ppend) & !blocked;
    if pending == 0 { return false; }

    let sig = pending.trailing_zeros() as usize;
    let bit = 1u64 << sig;
    /* 从产生它的那一级清位。两级都有同一位时先处理线程级（同步异常优先），
     * 进程级那一次会在下一个 trap 出口补投。 */
    if tpend & bit != 0 {
        manager.thread_mut(current_tid).sig_pending &= !bit;
    } else {
        manager.process_mut(current_pid).sig_pending &= !bit;
    }
    let action = manager.process(current_pid).sig_actions[sig];
    let old_blocked = blocked;
    drop(manager);

    match action {
        SigAction::Ignore => false,
        SigAction::Handler { entry, restorer } => {
            deliver_handler(cx, sig, entry, restorer, old_blocked);
            true
        }
        SigAction::Default => { apply_default_action(sig); false }
    }
}

/// sigprocmask(how, set_ptr, old_ptr)：how 0=BLOCK 1=UNBLOCK 2=SETMASK（与 libc 的
/// SIG_BLOCK/SIG_UNBLOCK/SIG_SETMASK 一致）。set_ptr 为 0 表示只查询。
/// 作用于当前线程（POSIX：屏蔽字是线程级的；多线程程序里应叫 `pthread_sigmask`）。
/// 用户态的 `sigset_t` 就是 8 字节（RK_NSIG=32），所以按 u64 小端读写。
pub fn sigprocmask(how: usize, set_ptr: usize, old_ptr: usize) -> isize {
    let tid = processor::current_tid();
    let old = {
        let manager = lock_detect!(TASK_MANAGER);
        match manager.try_thread(tid) {
            Some(t) => t.sig_blocked,
            None => return -1,
        }
    };

    /* 用户态 sigset_t 用 glibc 那套布局：bit(sig-1) 对应信号 sig；
     * 内核内部位图是 bit(sig)（和 sig_pending 一致）。所以两个方向的边界都要平移一位。
     * 忘掉这个平移的后果很难查：屏蔽了却照旧投递，看起来像"内核不认屏蔽字"。 */
    if old_ptr != 0 {
        let word = old >> 1;
        let mut buf = [0u8; 8];
        for i in 0..8 { buf[i] = ((word >> (i * 8)) & 0xff) as u8; }
        if write_current_user_bytes(old_ptr, &buf).is_none() { return -1; }
    }

    if set_ptr == 0 { return 0; }          /* 只查询 */

    let raw = match read_current_user_bytes(set_ptr, 8) {
        Some(b) => b,
        None => return -1,
    };
    let mut mask = 0u64;
    for i in 0..8 { mask |= (raw[i] as u64) << (i * 8); }
    mask = (mask << 1) & (u64::MAX << 1);   /* user bit(sig-1) -> kernel bit(sig) */
    /* SIGKILL / SIGSTOP 不可屏蔽：静默剔掉，不报错（POSIX 也没让报） */
    mask &= !((1u64 << super::SIGKILL) | (1u64 << super::SIGSTOP));

    let newmask = match how {
        0 => old | mask,        /* SIG_BLOCK */
        1 => old & !mask,       /* SIG_UNBLOCK */
        2 => mask,              /* SIG_SETMASK */
        _ => return -1,
    };

    let mut manager = lock_detect!(TASK_MANAGER);
    match manager.try_thread_mut(tid) {
        Some(t) => { t.sig_blocked = newmask; 0 }
        None => -1,
    }
}


fn deliver_handler(cx: &mut TrapContext, sig: usize, entry: usize, restorer: usize,
                   old_blocked: u64) {
    let frame_size = core::mem::size_of::<SigFrame>();

     let frame_addr = match cx.user_sp().checked_sub(frame_size) {
        Some(v) => v & !15usize,
        None => crate::task::exit_with_signal(super::SIGSEGV, false),
    };

    let frame = SigFrame { cx: *cx, signo: sig, blocked: old_blocked };
    let bytes = unsafe {
        core::slice::from_raw_parts(&frame as *const SigFrame as *const u8, frame_size)
    };

    if write_current_user_bytes(frame_addr, bytes).is_none() {
        /* 用户栈到底了，或者那段压根没映射。按栈溢出处理：SIGSEGV 终止。 */
        crate::task::exit_with_signal(super::SIGSEGV, false);
    }

    cx.set_sp(frame_addr);
    cx.set_user_pc(entry);
    cx.set_user_ra(restorer);
    cx.set_user_a0(sig);

    /* handler 期间屏蔽自身（POSIX 默认；SA_NODEFER 未实现）。
     * 这一步是防递归爆栈的关键：handler 里再 raise 同一个信号时，
     * 它只会挂到 pending，等 sigreturn 恢复屏蔽字后才补投。 */
    let mut manager = lock_detect!(TASK_MANAGER);
    if let Some(t) = manager.try_thread_mut(processor::current_tid()) {
        t.sig_blocked = old_blocked | (1u64 << sig);
    }
}

/// SIGRETURN：handler 里 `ret` 落到 trampoline，trampoline 发这个 syscall。
/// 从用户栈上那份 SigFrame 把整个现场恢复回来 —— 于是"回到被打断的地方"。
pub fn sigreturn_current(cx: &mut TrapContext) {
    let frame_size = core::mem::size_of::<SigFrame>();
    let sp = cx.user_sp();

    let bytes = match crate::task::read_current_user_bytes(sp, frame_size) {
        Some(b) => b,
        None => crate::task::exit_with_signal(super::SIGSEGV, false),
    };

    let mut frame = SigFrame { cx: *cx, signo: 0, blocked: 0 };
    unsafe {
        core::ptr::copy_nonoverlapping(
            bytes.as_ptr(),
            &mut frame as *mut SigFrame as *mut u8,
            frame_size,
        );
    }
    *cx = frame.cx;

    /* 恢复 handler 之前的屏蔽字。若 handler 里 raise 过自己，那个信号此刻
     * 已不再屏蔽 —— 本次 trap 出口的 do_signal 会立刻补投。 */
    let mut manager = lock_detect!(TASK_MANAGER);
    if let Some(t) = manager.try_thread_mut(processor::current_tid()) {
        t.sig_blocked = frame.blocked;
    }
}

/// 按信号死亡退出，并（可选）标记 core dump。
///
/// `core` 只影响 `waitpid` 报出的 wait status bit7（`WCOREDUMP`）——
/// 内核不写 core 文件（没有文件系统布局约定，也没有调试器来读它），
/// 但这个位必须真的置上：否则 `WIFSIGNALED` 有了、`WCOREDUMP` 永远 0，
/// 诊断工具会得出"段错误没有 core"的错误结论。
pub fn exit_with_signal(sig: usize, core: bool) -> ! {
    {
        let pid = current_pid();
        let mut manager = lock_detect!(TASK_MANAGER);
        if let Some(p) = manager.try_process_mut(pid) {
            /* `term_sig` 必须无条件记下：`waitpid` 的编码靠它判断"这次是被
             * 信号打死的"还是"自己 exit 的"，绝不能用 `exit_code >= 128`
             * 反推（见 process.rs 的 term_sig 注释）。`coredump` 只影响 bit7。 */
            p.term_sig = sig;
            if core {
                p.coredump = true;
            }
        }
        /* 锁必须在 exit 之前放掉：exit_current_and_run_next 自己会加锁。 */
        drop(manager);
    }
    crate::task::exit_current_and_run_next(128 + sig as i32)
}

/// 离开 CPU 但不改变自己"不可运行"的身份。
///
/// 和 `suspend_current_and_run_next()` 的唯一区别：不登记
/// `pending_ready_tid`。那一步的意思正是"我随时可以再跑"（让 run_tasks
/// 把我重新入队），而停止态要的正好相反 —— 只有 SIGCONT 能让我回来。
fn switch_away_stopped() {
    let current_tid = processor::current_tid();

    let task_cx_ptr = {
        let mut manager = lock_detect!(TASK_MANAGER);
        manager.thread_cx_ptr(current_tid)
    };

    let idle_cx_ptr = processor::idle_task_cx_ptr();

    crate::arch::disable_interrupt();
    unsafe {
        __switch(task_cx_ptr, idle_cx_ptr);
    }
    /* SIGCONT 之后从这里回来：状态已被 continue_process 改成 Ready/Running，
     * 继续往回走到 trap 出口即可（现场还是被停住那一刻的，一字未动）。 */
}

/// 若当前线程有一个"有效动作就是停止"的待投递信号，清位并返回它。
///
/// 给终端驱动用：进程阻塞在 `read(0)`（等键盘）时，Ctrl+Z 的正确行为是
/// 就地停住、SIGCONT 之后回到原地继续等键。若改成"返回 EINTR"，
/// `cat` 这类程序会走 `if (n < 0) { "read failed"; exit(1); }` —— 被 Ctrl+Z
/// 按成错误退出而不是暂停。交互程序普遍不写 EINTR 重试循环，指望不上。
///
/// 只在"默认动作 = Stop"时返回：装了 handler 的（想自己处理）和被 SIG_IGN 的
/// 都不该在这里被吃掉，它们该走正常的 EINTR → trap 出口 → do_signal 路径。
pub fn take_pending_stop_signal() -> Option<usize> {
    let pid = current_pid();
    let tid = processor::current_tid();
    let mut manager = lock_detect!(TASK_MANAGER);
    manager.take_pending_stop(tid, pid)
}

/// 停住当前进程（默认动作 Stop 的实现）。
///
/// 也供终端驱动调用：交互程序阻塞在 `read(0)` 上时，Ctrl+Z 必须就地停住它，
/// 见 `take_pending_stop_signal` 的注释。
pub fn stop_current_process(sig: usize) {
    let pid = current_pid();
    let mut need_ipi = false;

    {
        let mut manager = lock_detect!(TASK_MANAGER);
        if manager.stop_process(pid, sig) {
            /* 父进程正阻塞在 waitpid(WUNTRACED) 上等这个孩子 —— 不叫醒它，
             * Ctrl+Z 看起来就是整台机器卡住。 */
            if manager.wake_parent_waiting_for(pid) {
                need_ipi = true;
            }
        }
    }

    if need_ipi {
        ipi::send_ipi_to_others(ipi::IpiKind::Reschedule, 0);
    }

    switch_away_stopped();
}

/// 执行某个信号的默认动作。
fn apply_default_action(sig: usize) {
    match super::default_action(sig) {
        DefAction::Term => exit_with_signal(sig, false),
        /* Core 与 Term 的唯一区别就是这个 bit7。 */
        DefAction::Core => exit_with_signal(sig, true),
        DefAction::Ign => {}
        DefAction::Stop => stop_current_process(sig),
        /* 继续的动作已经在 `kill()` 里做完了（停止态的进程根本跑不到这里）。
         * 走到这一支只说明"进程正在跑，又收到一个 SIGCONT" —— 本来就该无事发生。 */
        DefAction::Cont => {}
    }
}

/// 置线程级 pending。只给同步异常用（SIGSEGV/SIGILL 一类）：
/// 这类信号必须由出错的那个线程自己处理，否则 handler 会跑在别人的栈上。
pub fn set_current_sig_pending(sig: usize) {
    if sig >= super::NSIG { return; }
    let tid = processor::current_tid();
    let mut manager = lock_detect!(TASK_MANAGER);
    if let Some(t) = manager.try_thread_mut(tid) {
        t.sig_pending |= 1u64 << sig;
    }
}


static SWITCH_BACK_MASK: AtomicUsize = AtomicUsize::new(0);
static LAST_SMP_DEBUG_TICK: AtomicUsize = AtomicUsize::new(0);

fn maybe_dump_smp_debug(tag: &str) {
    static LAST_SMP_DEBUG_TICK: AtomicUsize = AtomicUsize::new(0);

    let now = crate::timer::ticks();
    let last = LAST_SMP_DEBUG_TICK.load(Ordering::Relaxed);

    if now.wrapping_sub(last) < 500 {
        return;
    }

    if LAST_SMP_DEBUG_TICK
        .compare_exchange(last, now, Ordering::AcqRel, Ordering::Relaxed)
        .is_err()
    {
        return;
    }

    log::warn!("[smp-debug] tag={} tick={}", tag, now);

    crate::timer::dump_timer_masks();
    dump_preempt_masks();

    match TASK_MANAGER.try_lock() {
        Some(manager) => {
            manager.dump_tasks();
        }
        None => {
            log::warn!(
                "[smp-debug] TASK_MANAGER locked owner={} line={}",
                TASK_MANAGER.debug_owner(),
                TASK_MANAGER.debug_line(),
            );
        }
    }
}


fn mark_preempt_enter() {
    let hart = crate::arch::hartid();

    if hart < crate::arch::MAX_HARTS {
        PREEMPT_ENTER_MASK.fetch_or(1usize << hart, Ordering::Relaxed);
    }
}

fn mark_switch_back() {
    let hart = crate::arch::hartid();

    if hart < crate::arch::MAX_HARTS {
        SWITCH_BACK_MASK.fetch_or(1usize << hart, Ordering::Relaxed);
    }
}

pub fn dump_preempt_masks() {
    log::warn!(
        "[preempt-mask] enter={:#x} back={:#x}",
        PREEMPT_ENTER_MASK.load(Ordering::Relaxed),
        SWITCH_BACK_MASK.load(Ordering::Relaxed),
    );
}

static LAST_SWITCH_BACK_TICK: AtomicUsize = AtomicUsize::new(0);

pub fn mark_switch_back_tick() {
    LAST_SWITCH_BACK_TICK.store(crate::timer::ticks(), Ordering::Release);
}

pub fn last_switch_back_tick() -> usize {
    LAST_SWITCH_BACK_TICK.load(Ordering::Acquire)
}

pub fn dump_task_manager_lock_state() {
    log::warn!(
        "[task-lock] locked={} owner={} line={}",
        TASK_MANAGER.debug_is_locked(),
        TASK_MANAGER.debug_owner(),
        TASK_MANAGER.debug_line(),
    );
}

/// 读取当前进程的附加组列表, 返回 (groups, ngroups) 的副本。
///
/// boot 阶段无 current task 时返回空组（配合 `current_creds` 的 root 判定，
/// 权限检查走 root 分支，不看附加组）。
pub fn current_groups() -> ([usize; NGROUPS_MAX], usize) {
    let tid = match processor::current_tid_opt() {
        Some(t) => t,
        None => return ([0; NGROUPS_MAX], 0),
    };
    let manager = lock_detect!(TASK_MANAGER);
    let pid = manager.pid_of_tid(tid);
    let p = manager.process(pid);
    (p.groups, p.ngroups)
}

/// 设置当前进程的附加组列表(仅由特权进程经 setgroups 调用)。
/// ngroups 超过上限时被截断到 NGROUPS_MAX。
pub fn set_current_groups(groups: [usize; NGROUPS_MAX], ngroups: usize) {
    let tid = processor::current_tid();
    let mut manager = lock_detect!(TASK_MANAGER);
    let pid = manager.pid_of_tid(tid);
    let p = manager.process_mut(pid);
    p.groups = groups;
    p.ngroups = ngroups.min(NGROUPS_MAX);
}
