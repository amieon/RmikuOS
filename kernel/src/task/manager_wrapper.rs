use core::sync::atomic::{AtomicBool, AtomicUsize, Ordering};

use alloc::string::String;
use alloc::vec::Vec;
use log::logger;

use crate::arch::{MAX_HARTS, ipi};                    // 新增：IPI 发送接口
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
/// (v1 从简用广播;阶段三可改单播 + 按 hart 踢。)
fn kick_idle_harts() {
    for h in 0..MAX_HARTS {
        if IDLE_HARTS[h].load(Ordering::Acquire) {
            ipi::send_ipi_to_others(ipi::IpiKind::Reschedule, 0);
            return;
        }
    }
}

pub fn init() {
    let init_path = "/bin/shell";

    let app = crate::fs::read_all(init_path)
        .expect("[task] failed to load init shell from /bin/shell");

    let (user_space, entry, user_sp) =
        crate::mm::MemorySet::from_elf(app.as_slice()).unwrap();

    let trap_cx =
        crate::trap::TrapContext::app_init_context(entry, user_sp);

    let mut process = ProcessControlBlock::new(
        0,
        user_space,
        String::from("/"),
    );

    let thread = ThreadControlBlock::new_main_thread(
        0,
        0,
        trap_cx,
    );

    process.threads.push(0);
    process.ready_threads.push(0);

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
        let next_tid = {
            let mut manager = lock_detect!(TASK_MANAGER);  // lock 内部已 preempt_disable
            let now = crate::timer::ticks();
            manager.wake_sleeping_threads(now);
            manager.find_next_ready_thread()
        };  // 此处 drop guard → unlock → preempt_enable

        //ipi::send_ipi_to_others(ipi::IpiKind::Reschedule, 0);

        if let Some(tid) = next_tid {
            let (pid, root_ppn, kernel_stack_top, trap_cx_addr, task_cx_ptr) = {
                let mut manager = lock_detect!(TASK_MANAGER);
                manager.prepare_thread(tid)
            };

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
                        log::error!(
                            "[sched] hart {} back from tid {}, but running_on={:?}",
                            hart,
                            tid,
                            thread.running_on,
                        );
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
    crate::syscall::bkl_unlock_if_held_by_current();
    // sleep 不会使其他线程就绪，无需 IPI
    unsafe {
        __switch(task_cx_ptr, idle_cx_ptr);
    }

    0
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

    crate::syscall::bkl_unlock_if_held_by_current();
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
        let task_cx_ptr = {
            let mut manager = lock_detect!(TASK_MANAGER);
            let current_pid = manager.pid_of_tid(current_tid);

            match manager.try_waitpid(current_pid, pid, exit_code_ptr) {
                WaitPidAction::Return(ret) => {
                    return ret;
                }

                WaitPidAction::Block => {
                    if nohang {
                        return 0;
                    }

                    log::info!(
                        "[task] tid {} waitpid(pid={}) blocking",
                        current_tid,
                        pid,
                    );

                    manager.block_thread(
                        current_tid,
                        BlockReason::WaitPid { pid },
                    );

                    manager.thread_cx_ptr(current_tid)
                }
            }
        };

        let idle_cx_ptr = processor::idle_task_cx_ptr();
        crate::syscall::bkl_unlock_if_held_by_current();
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
            child_free_fds,
            child_cwd,
            parent_tickets,
            parent_pass,
            child_mmap_areas,
            child_mmap_free_areas,
            child_mmap_next,
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

    // 新增：fork 产生了新的就绪线程，通知其他核
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

pub fn current_task_id() -> usize {
    current_pid()
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

pub fn exec_current(path_ptr: usize, path_len: usize, args_ptr: usize) -> isize {
    let path_bytes = match crate::task::read_current_user_bytes(path_ptr, path_len) {
        Some(bytes) => bytes,
        None => return -1,
    };

    let name = match core::str::from_utf8(&path_bytes) {
        Ok(s) => s.trim_matches('\0').trim(),
        Err(_) => return -1,
    };

    if name.is_empty() {
        return -1;
    }

    let cwd = crate::task::current_cwd();
    let mut path_buf = String::new();

    let path = if name.starts_with('/') || name.starts_with("./") || name.starts_with("../") {
        match crate::fs::normalize_path(&cwd, name) {
            Some(p) => {
                path_buf = p;
                path_buf.as_str()
            }
            None => return -1,
        }
    } else {
        path_buf.push_str("/bin/");
        path_buf.push_str(name);
        path_buf.as_str()
    };

    let argv = match read_exec_args(args_ptr, name) {
        Some(argv) => argv,
        None => return -1,
    };

    let argc = argv.len();

    let file = match crate::fs::open(path, crate::fs::flag::O_RDONLY) {
        Some(file) => file,
        None => {
            log::info!("[exec] open failed: {}", path);
            return -1;
        }
    };

    if file.is_dir() || !file.readable() {
        log::warn!("[exec] not a regular readable file: {}", path);
        return -1;
    }

    let mut app_data = Vec::new();
    let mut buf = [0u8; 512];

    loop {
        let n = file.read(&mut buf);

        if n < 0 {
            log::warn!("[exec] read failed: {}", path);
            return -1;
        }

        if n == 0 {
            break;
        }

        app_data.extend_from_slice(&buf[..n as usize]);
    }

    if app_data.is_empty() {
        log::warn!("[exec] empty executable: {}", path);
        return -1;
    }

    let (new_user_space, entry, user_sp) =
    match crate::mm::MemorySet::from_elf(&app_data) {
        Some(v) => v,
        None => {
            log::warn!("[exec] invalid ELF executable: {}", path);
            return -1;
        }
    };;

    let (new_user_sp, argv_ptr) = match build_user_stack_with_args(
        &new_user_space,
        user_sp,
        &argv,
    ) {
        Some(v) => v,
        None => return -1,
    };

    let mut new_trap_cx =
        crate::trap::TrapContext::app_init_context(entry, new_user_sp);

    new_trap_cx.set_app_args(argc, argv_ptr);

    let current_tid = processor::current_tid();

    let new_root = {
        let mut manager = lock_detect!(TASK_MANAGER);
        let current_pid = manager.pid_of_tid(current_tid);

        if manager.process(current_pid).threads.len() != 1 {
            log::info!(
                "[exec] pid={} has multiple threads, exec denied in first version",
                current_pid,
            );
            return -1;
        }

        let (old_space, closed_files) = {
            let process = manager.process_mut(current_pid);
            let closed = process.close_non_standard_fds_on_exec();
            let old = core::mem::replace(&mut process.user_space, new_user_space);
            (old, closed)
        }; 


        for file in &closed_files {
            manager.release_file(file);
        }

        *manager.thread_mut(current_tid).trap_cx_mut() = new_trap_cx;

        let root = manager.process(current_pid).user_space.root_ppn();

        drop(old_space);

        root
    };

    crate::mm::activate_page_table(new_root);

    argc as isize
}

fn read_exec_args(
    args_ptr: usize,
    default_argv0: &str,
) -> Option<Vec<Vec<u8>>> {
    if args_ptr == 0 {
        let mut argv = Vec::new();
        argv.push(default_argv0.as_bytes().to_vec());
        return Some(argv);
    }

    let bytes = crate::task::read_current_user_bytes(
        args_ptr,
        core::mem::size_of::<UserExecArgs>(),
    )?;

    let raw = unsafe {
        core::ptr::read_unaligned(bytes.as_ptr() as *const UserExecArgs)
    };

    if raw.argc == 0 || raw.argc > EXEC_MAX_ARGS {
        return None;
    }

    let mut argv = Vec::new();

    for i in 0..raw.argc {
        let arg = raw.argv[i];

        if arg.ptr == 0 || arg.len > 256 {
            return None;
        }

        let mut data = crate::task::read_current_user_bytes(arg.ptr, arg.len)?;

        if let Some(pos) = data.iter().position(|&c| c == 0) {
            data.truncate(pos);
        }

        argv.push(data);
    }

    Some(argv)
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
) -> Option<(usize, usize)> {
    let mut sp = user_sp;
    let mut arg_ptrs: Vec<usize> = Vec::new();

    for arg in argv.iter().rev() {
        sp -= arg.len() + 1;

        write_to_user_space(user_space, sp, arg)?;
        write_to_user_space(user_space, sp + arg.len(), &[0])?;

        arg_ptrs.push(sp);
    }

    arg_ptrs.reverse();

    sp = align_down(sp, 16);

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

pub fn get_fd_flags_current(fd: usize) -> usize{
    let current_pid = current_pid();
    let mut manager = lock_detect!(TASK_MANAGER);
    let process = manager.process_mut(current_pid);
    process.fd_flags.get(fd).copied().unwrap_or(0)
}

pub fn set_fcntl(fd: usize, cmd: usize, arg: usize) -> isize {
    let pid = current_pid();
    let mut manager = lock_detect!(TASK_MANAGER);
    let process = manager.process_mut(pid);
    if fd >= process.fd_table.len() || process.fd_table[fd].is_none() {
        return -1;
    }
    match cmd {
        crate::fs::F_GETFL => process.fd_flags.get(fd).copied().unwrap_or(0) as isize,
        crate::fs::F_SETFL => {
            if fd >= process.fd_flags.len() {
                process.fd_flags.resize(fd + 1, 0);
            }
            process.fd_flags[fd] = arg;
            0
        }
        _ => -1,
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

        for tid in tids {
            if tid == current_tid {
                let thread = manager.thread_mut(tid);
                thread.status = ThreadStatus::Zombie;
                thread.block_reason = BlockReason::None;
                thread.exit_code = exit_code;
            } else if let Some(thread) = manager.try_thread_mut(tid) {
                thread.status = ThreadStatus::Dead;
                thread.block_reason = BlockReason::None;
                thread.exit_code = exit_code;
            }
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
    crate::syscall::bkl_unlock_if_held_by_current();

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

        {
            let thread = manager.thread_mut(current_tid);
            thread.status = ThreadStatus::Zombie;
            thread.block_reason = BlockReason::None;
            thread.exit_code = exit_code;
        }

        //manager.wake_threads_joining(current_tid);

        manager.thread_cx_ptr(current_tid)
    };

    let idle_cx_ptr = processor::idle_task_cx_ptr();
    crate::syscall::bkl_unlock_if_held_by_current();

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
        crate::syscall::bkl_unlock_if_held_by_current();
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
            // 拿不到锁不再丢账:暂存到 per-hart 缓冲,下次拿到锁时冲刷。
            // 归属正确性见 Processor::pending_ticks 的不变式注释。
            processor::add_pending_tick();
            return;
        }
    };

    // 冲刷缓冲:pending 全部属于当前 tid(见 processor.rs 不变式)。
    // 极端情况(线程在丢账后、冲刷前退出)会把少量 tick 记给继任者,
    // 数量以 slice 为界,对窗口级统计无影响。
    let n = 1 + processor::take_pending_ticks();

    let pid = manager.pid_of_tid(tid);
    manager.thread_mut(tid).run_ticks += n;
    manager.process_mut(pid).run_ticks += n;
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

        let runnable_threads = manager
            .count_sched_runnable_threads_in_process(pid)
            .max(1);

        let alpha = manager.get_sched_alpha();
        let factor = crate::math::sched_thread_scale(runnable_threads, alpha);

        let tickets = process.tickets.max(1);

        let effective_tickets = tickets
            .saturating_mul(factor)
            .max(1);

        let stride =
            crate::task::process::stride_from_tickets(effective_tickets);

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
    crate::syscall::bkl_unlock_if_held_by_current();
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
    crate::syscall::bkl_unlock_if_held_by_current();
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

pub fn dup2(old_fd : usize,new_fd : usize) -> isize{
    let mut manager = lock_detect!(TASK_MANAGER);
    let current_tid = processor::current_tid();
    let current_pid = manager.pid_of_tid(current_tid);
    
    if old_fd == new_fd {
        return new_fd as isize;
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
            return -1;
        }
        process.fd_table[new_fd].take()
    };
    
    if let Some(f) = &new_file {
        manager.release_file(f);
    }

    old_file.on_fork(); 
    {
        let process = manager.process_mut(current_pid);
        process.fd_table[new_fd] = Some(old_file);
    }

    new_fd as isize
}

pub fn kill(pid: usize, sig: usize) -> isize {
    if sig >= 64 {
        return -1;
    }

    let mut need_ipi = false;

    {
        let mut manager = lock_detect!(TASK_MANAGER);

        if manager.try_process(pid).is_none() {
            return -1;
        }

        manager.process_mut(pid).sig_pending |= 1u64 << sig;

        let tids = manager.process(pid).threads.clone();

        for tid in tids {
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

pub fn do_signal() {
    let current_pid = current_pid();
    let mut manager = lock_detect!(TASK_MANAGER);
    let process = manager.process_mut(current_pid);
    let pending = process.sig_pending;
    if pending == 0 { return; }
    
    let fatal_bits = pending & super::FATAL_SIG_MASK;
    if fatal_bits != 0 {
        let sig = fatal_bits.trailing_zeros() as usize;
        process.sig_pending = 0;
        drop(manager);
        crate::task::exit_current_and_run_next(128 + sig as i32);
    }
    process.sig_pending = 0;
}

pub fn set_current_sig_pending(sig : usize){
    let pid = current_pid();
    if sig >= 64 { return; }
    let mut manager = lock_detect!(TASK_MANAGER);
    if let Some(process) = manager.try_process_mut(pid) {
        process.sig_pending |= 1u64 << sig;
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