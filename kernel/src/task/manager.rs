use alloc::string::String;
use alloc::vec::Vec;

use crate::mm::{MemorySet, PhysPageNum, VirtAddr, PAGE_SIZE_BITS};
use crate::mm::config::PAGE_SIZE;
use crate::sync::spin::Mutex;
use crate::trap::TrapContext;

use super::context::TaskContext;
use super::process::{Pid, ProcessControlBlock};
use super::processor;
use super::switch::__switch;
use super::thread::{BlockReason, ThreadControlBlock, ThreadStatus, Tid};

unsafe extern "C" {
    fn __restore_user(cx: *const TrapContext) -> !;
}

static TASK_MANAGER: Mutex<TaskManager> = Mutex::new(TaskManager::new());

pub struct TaskManager {
    processes: Vec<Option<ProcessControlBlock>>,
    threads: Vec<Option<ThreadControlBlock>>,

    free_pids: Vec<Pid>,
    free_tids: Vec<Tid>,
}

enum WaitPidAction {
    Return(isize),
    Block,
}

impl TaskManager {
    pub const fn new() -> Self {
        Self {
            processes: Vec::new(),
            threads: Vec::new(),

            free_pids: Vec::new(),
            free_tids: Vec::new(),
        }
    }

    fn alloc_pid(&mut self) -> Pid {
        self.free_pids.pop().unwrap_or(self.processes.len())
    }

    fn alloc_tid(&mut self) -> Tid {
        self.free_tids.pop().unwrap_or(self.threads.len())
    }

    fn insert_process(&mut self, process: ProcessControlBlock) {
        let pid = process.pid;

        if pid >= self.processes.len() {
            self.processes.resize_with(pid + 1, || None);
        }

        assert!(
            self.processes[pid].is_none(),
            "[task] process slot already used: pid={}",
            pid,
        );

        self.processes[pid] = Some(process);
    }

    fn insert_thread(&mut self, thread: ThreadControlBlock) {
        let tid = thread.tid;

        if tid >= self.threads.len() {
            self.threads.resize_with(tid + 1, || None);
        }

        assert!(
            self.threads[tid].is_none(),
            "[task] thread slot already used: tid={}",
            tid,
        );

        self.threads[tid] = Some(thread);
    }

    pub fn process(&self, pid: Pid) -> &ProcessControlBlock {
        self.processes
            .get(pid)
            .and_then(|slot| slot.as_ref())
            .expect("[task] invalid pid")
    }

    fn process_mut(&mut self, pid: Pid) -> &mut ProcessControlBlock {
        self.processes
            .get_mut(pid)
            .and_then(|slot| slot.as_mut())
            .expect("[task] invalid pid")
    }

    pub fn thread(&self, tid: Tid) -> &ThreadControlBlock {
        self.threads
            .get(tid)
            .and_then(|slot| slot.as_ref())
            .expect("[task] invalid tid")
    }

    fn thread_mut(&mut self, tid: Tid) -> &mut ThreadControlBlock {
        self.threads
            .get_mut(tid)
            .and_then(|slot| slot.as_mut())
            .expect("[task] invalid tid")
    }

    fn try_process(&self, pid: Pid) -> Option<&ProcessControlBlock> {
        self.processes.get(pid)?.as_ref()
    }

    fn try_thread(&self, tid: Tid) -> Option<&ThreadControlBlock> {
        self.threads.get(tid)?.as_ref()
    }

    fn pid_of_tid(&self, tid: Tid) -> Pid {
        self.thread(tid).pid
    }

    fn current_pid(&self) -> Pid {
        let tid = processor::current_tid();
        self.pid_of_tid(tid)
    }

    fn enqueue_ready_thread(&mut self, tid: Tid) {
        let pid = self.thread(tid).pid;

        if self.thread(tid).status != ThreadStatus::Ready {
            return;
        }

        if !self.process(pid).ready_threads.contains(&tid) {
            self.process_mut(pid).ready_threads.push(tid);
        }
    }

    fn process_has_ready_thread(&self, pid: Pid) -> bool {
        let Some(process) = self.try_process(pid) else {
            return false;
        };

        process.ready_threads.iter().any(|&tid| {
            self.try_thread(tid)
                .map(|thread| thread.status == ThreadStatus::Ready)
                .unwrap_or(false)
        })
    }

    fn find_next_ready_thread(&mut self) -> Option<Tid> {
        let mut best: Option<(Pid, usize)> = None;

        for pid in 0..self.processes.len() {
            let Some(process) = self.processes[pid].as_ref() else {
                continue;
            };

            if !self.process_has_ready_thread(pid) {
                continue;
            }

            match best {
                None => best = Some((pid, process.pass)),
                Some((_, best_pass)) if process.pass < best_pass => {
                    best = Some((pid, process.pass));
                }
                _ => {}
            }
        }

        let pid = best.map(|(pid, _)| pid)?;

        {
            let process = self.process_mut(pid);
            process.pass = process.pass.wrapping_add(process.stride);
        }

        self.pick_ready_thread_in_process(pid)
    }

    fn pick_ready_thread_in_process(&mut self, pid: Pid) -> Option<Tid> {
        loop {
            let tid = {
                let process = self.process_mut(pid);

                if process.ready_threads.is_empty() {
                    return None;
                }

                process.ready_threads.remove(0)
            };

            let Some(thread) = self.try_thread(tid) else {
                continue;
            };

            if thread.status == ThreadStatus::Ready {
                return Some(tid);
            }
        }
    }

    fn mark_thread_ready(&mut self, tid: Tid) {
        {
            let thread = self.thread_mut(tid);

            if thread.status == ThreadStatus::Running {
                thread.status = ThreadStatus::Ready;
            }
        }

        self.enqueue_ready_thread(tid);
    }

    fn mark_thread_zombie(&mut self, tid: Tid, exit_code: i32) {
        let pid = self.thread(tid).pid;

        {
            let thread = self.thread_mut(tid);
            thread.status = ThreadStatus::Zombie;
            thread.block_reason = BlockReason::None;
            thread.exit_code = exit_code;
        }

        self.process_mut(pid).exit_code = exit_code;
    }

    fn thread_cx_ptr(&mut self, tid: Tid) -> *mut TaskContext {
        self.thread_mut(tid).task_cx_ptr()
    }

    fn prepare_thread(
        &mut self,
        tid: Tid,
    ) -> (Pid, PhysPageNum, usize, usize, *mut TaskContext) {
        let pid = self.thread(tid).pid;
        let root = self.process(pid).user_space.root_ppn();

        let thread = self.thread_mut(tid);

        thread.kernel_stack.check_guard();
        thread.status = ThreadStatus::Running;

        (
            pid,
            root,
            thread.kernel_stack.top(),
            thread.trap_cx_addr,
            thread.task_cx_ptr(),
        )
    }

    fn block_thread(&mut self, tid: Tid, reason: BlockReason) {
        let thread = self.thread_mut(tid);
        thread.status = ThreadStatus::Blocking;
        thread.block_reason = reason;
    }

    fn wake_sleeping_threads(&mut self, now: usize) {
        let mut wake_list = Vec::new();

        for tid in 0..self.threads.len() {
            let Some(thread) = self.threads[tid].as_ref() else {
                continue;
            };

            if thread.status != ThreadStatus::Blocking {
                continue;
            }

            match thread.block_reason {
                BlockReason::Sleep { wake_tick } if now >= wake_tick => {
                    wake_list.push(tid);
                }
                _ => {}
            }
        }

        for tid in wake_list {
            let wake_tick = match self.thread(tid).block_reason {
                BlockReason::Sleep { wake_tick } => wake_tick,
                _ => 0,
            };

            {
                let thread = self.thread_mut(tid);

                log::info!(
                    "[task] wake thread {} from sleep: now={}, wake_tick={}",
                    tid,
                    now,
                    wake_tick,
                );

                thread.status = ThreadStatus::Ready;
                thread.block_reason = BlockReason::None;
            }

            self.enqueue_ready_thread(tid);
        }
    }

    pub fn read_current_user_bytes(&self, user_buf: usize, len: usize) -> Option<Vec<u8>> {
        let pid = self.current_pid();
        let process = self.try_process(pid)?;

        let mut bytes = Vec::new();

        for offset in 0..len {
            let va = user_buf.checked_add(offset)?;
            let vpn = VirtAddr(va).floor();
            let page_offset = va & (PAGE_SIZE - 1);

            let pte = process.user_space.translate(vpn)?;

            let pa = (pte.ppn().0 << PAGE_SIZE_BITS) + page_offset;
            let kva = crate::mm::kernel_phys_to_virt(pa);

            let byte = unsafe {
                core::ptr::read_volatile(kva as *const u8)
            };

            bytes.push(byte);
        }

        Some(bytes)
    }

    fn write_user_bytes_by_pid(
        &self,
        pid: Pid,
        user_buf: usize,
        data: &[u8],
    ) -> Option<usize> {
        let process = self.try_process(pid)?;

        for (offset, byte) in data.iter().enumerate() {
            let va = user_buf.checked_add(offset)?;
            let vpn = VirtAddr(va).floor();
            let page_offset = va & (PAGE_SIZE - 1);

            let pte = process.user_space.translate(vpn)?;

            let pa = (pte.ppn().0 << PAGE_SIZE_BITS) + page_offset;
            let kva = crate::mm::kernel_phys_to_virt(pa);

            unsafe {
                core::ptr::write_volatile(kva as *mut u8, *byte);
            }
        }

        Some(data.len())
    }

    fn write_user_i32(&self, pid: Pid, user_ptr: usize, value: i32) -> Option<()> {
        if user_ptr == 0 {
            return Some(());
        }

        let bytes = value.to_ne_bytes();
        self.write_user_bytes_by_pid(pid, user_ptr, &bytes)?;
        Some(())
    }

    fn process_is_zombie(&self, pid: Pid) -> bool {
        let Some(process) = self.try_process(pid) else {
            return false;
        };

        if process.threads.is_empty() {
            return false;
        }

        process.threads.iter().all(|&tid| {
            matches!(
                self.try_thread(tid).map(|thread| thread.status),
                Some(ThreadStatus::Zombie | ThreadStatus::Dead)
            )
        })
    }

    fn reap_process(&mut self, pid: Pid) {
        let process = self
            .processes
            .get_mut(pid)
            .expect("[task] invalid reap pid")
            .take();

        let Some(process) = process else {
            panic!("[task] reap empty process slot: pid={}", pid);
        };

        for tid in process.threads {
            if let Some(slot) = self.threads.get_mut(tid) {
                let old = slot.take();

                if old.is_some() {
                    self.free_tids.push(tid);
                }
            }
        }

        self.free_pids.push(pid);
    }

    fn wake_parent_waiting_for(&mut self, child_pid: Pid) {
        let parent_pid = match self.try_process(child_pid).and_then(|process| process.parent) {
            Some(parent_pid) => parent_pid,
            None => return,
        };

        let parent_threads = match self.try_process(parent_pid) {
            Some(parent) => parent.threads.clone(),
            None => return,
        };

        for tid in parent_threads {
            let should_wake = match self.try_thread(tid) {
                Some(thread) if thread.status == ThreadStatus::Blocking => {
                    match thread.block_reason {
                        BlockReason::WaitPid { pid } => {
                            pid == -1 || pid as usize == child_pid
                        }
                        _ => false,
                    }
                }
                _ => false,
            };

            if should_wake {
                {
                    let thread = self.thread_mut(tid);
                    thread.status = ThreadStatus::Ready;
                    thread.block_reason = BlockReason::None;
                }

                self.enqueue_ready_thread(tid);

                log::info!(
                    "[task] wake parent pid={} tid={} waiting for child {}",
                    parent_pid,
                    tid,
                    child_pid,
                );
            }
        }
    }

    fn try_waitpid(
        &mut self,
        current_pid: Pid,
        pid: isize,
        exit_code_ptr: usize,
    ) -> WaitPidAction {
        if self.process(current_pid).children.is_empty() {
            return WaitPidAction::Return(-1);
        }

        let mut has_matched_child = false;
        let children_snapshot = self.process(current_pid).children.clone();

        for child_pid in children_snapshot {
            let matched = pid == -1 || pid as usize == child_pid;

            if !matched {
                continue;
            }

            has_matched_child = true;

            if !self.process_is_zombie(child_pid) {
                continue;
            }

            let code = self
                .try_process(child_pid)
                .map(|child| child.exit_code)
                .unwrap_or(0);

            if self
                .write_user_i32(current_pid, exit_code_ptr, code)
                .is_none()
            {
                return WaitPidAction::Return(-1);
            }

            self.process_mut(current_pid)
                .children
                .retain(|&x| x != child_pid);

            self.reap_process(child_pid);

            log::info!(
                "[task] pid {} collected child {}, exit_code={}",
                current_pid,
                child_pid,
                code,
            );

            return WaitPidAction::Return(child_pid as isize);
        }

        if !has_matched_child {
            return WaitPidAction::Return(-1);
        }

        WaitPidAction::Block
    }

    fn get_file(&self, pid: Pid, fd: usize) -> Option<crate::fs::FileRef> {
        self.process(pid)
            .fd_table
            .get(fd)?
            .as_ref()
            .cloned()
    }

    fn alloc_fd(&mut self, pid: Pid, file: crate::fs::FileRef) -> isize {
        let process = self.process_mut(pid);

        if let Some(fd) = process.free_fds.pop() {
            assert!(
                fd < process.fd_table.len(),
                "free fd out of range: fd={}, len={}",
                fd,
                process.fd_table.len(),
            );

            assert!(
                process.fd_table[fd].is_none(),
                "free fd slot is not empty: fd={}",
                fd,
            );

            process.fd_table[fd] = Some(file);
            return fd as isize;
        }

        let fd = process.fd_table.len();
        process.fd_table.push(Some(file));
        fd as isize
    }

    fn close_fd(&mut self, pid: Pid, fd: usize) -> isize {
        let process = self.process_mut(pid);

        if fd >= process.fd_table.len() {
            return -1;
        }

        if process.fd_table[fd].take().is_none() {
            return -1;
        }

        process.free_fds.push(fd);

        0
    }
}

pub fn init() {
    let init_path = "/bin/shell";

    let app = crate::fs::read_all(init_path)
        .expect("[task] failed to load init shell from /bin/shell");

    let (user_space, entry, user_sp) =
        crate::mm::MemorySet::new_user_test(app.as_slice());

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

    let mut manager = TASK_MANAGER.lock();
    manager.insert_process(process);
    manager.insert_thread(thread);

    log::info!("[task] loaded init shell as pid=0 tid=0");
}

pub fn run_first_task() -> ! {
    run_tasks()
}

pub fn run_tasks() -> ! {
    loop {
        let next = {
            let mut manager = TASK_MANAGER.lock();
            let now = crate::timer::ticks();

            manager.wake_sleeping_threads(now);

            if let Some(tid) = manager.find_next_ready_thread() {
                let (pid, root, kstack_top, trap_cx_addr, task_cx_ptr) =
                    manager.prepare_thread(tid);

                log::info!(
                    "[task] schedule pid={} tid={}: root={:?}, kstack_top={:#x}, trap_cx={:#x}",
                    pid,
                    tid,
                    root,
                    kstack_top,
                    trap_cx_addr,
                );

                Some((tid, root, task_cx_ptr))
            } else {
                None
            }
        };

        if let Some((tid, root, task_cx_ptr)) = next {
            processor::set_current_tid(Some(tid));

            crate::mm::activate_page_table(root);

            let idle_cx_ptr = processor::idle_task_cx_ptr();

            unsafe {
                __switch(idle_cx_ptr, task_cx_ptr);
            }

            processor::set_current_tid(None);
        } else {
            crate::arch::enable_interrupt();
            crate::arch::wait_for_interrupt();
            crate::arch::disable_interrupt();
        }
    }
}

#[no_mangle]
pub extern "C" fn __task_entry() -> ! {
    let current_tid = processor::current_tid();

    let trap_cx_addr = {
        let manager = TASK_MANAGER.lock();
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
        let mut manager = TASK_MANAGER.lock();

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

    unsafe {
        __switch(task_cx_ptr, idle_cx_ptr);
    }

    0
}

pub fn suspend_current_and_run_next() -> isize {
    let current_tid = processor::current_tid();

    let task_cx_ptr = {
        let mut manager = TASK_MANAGER.lock();
        manager.mark_thread_ready(current_tid);
        manager.thread_cx_ptr(current_tid)
    };

    let idle_cx_ptr = processor::idle_task_cx_ptr();

    unsafe {
        __switch(task_cx_ptr, idle_cx_ptr);
    }

    0
}

pub fn preempt_current_and_run_next() {
    let current_tid = processor::current_tid();

    let task_cx_ptr = {
        let mut manager = TASK_MANAGER.lock();
        manager.mark_thread_ready(current_tid);
        manager.thread_cx_ptr(current_tid)
    };

    let idle_cx_ptr = processor::idle_task_cx_ptr();

    unsafe {
        __switch(task_cx_ptr, idle_cx_ptr);
    }
}

pub fn waitpid_current(pid: isize, exit_code_ptr: usize) -> isize {
    let current_tid = processor::current_tid();

    loop {
        let action = {
            let mut manager = TASK_MANAGER.lock();
            let current_pid = manager.pid_of_tid(current_tid);
            manager.try_waitpid(current_pid, pid, exit_code_ptr)
        };

        match action {
            WaitPidAction::Return(ret) => return ret,
            WaitPidAction::Block => {
                let task_cx_ptr = {
                    let mut manager = TASK_MANAGER.lock();

                    log::info!(
                        "[task] tid {} waitpid(pid={}) blocking",
                        current_tid,
                        pid,
                    );

                    manager.block_thread(
                        current_tid,
                        BlockReason::WaitPid {
                            pid,
                        },
                    );

                    manager.thread_cx_ptr(current_tid)
                };

                let idle_cx_ptr = processor::idle_task_cx_ptr();

                unsafe {
                    __switch(task_cx_ptr, idle_cx_ptr);
                }
            }
        }
    }
}

pub fn fork_current() -> isize {
    let parent_tid = processor::current_tid();

    let child_pid = {
        let mut manager = TASK_MANAGER.lock();

        let parent_pid = manager.pid_of_tid(parent_tid);

        let child_pid = manager.alloc_pid();
        let child_tid = manager.alloc_tid();

        let child_user_space =
            MemorySet::from_existed_user(&manager.process(parent_pid).user_space);

        let mut child_trap_cx =
            *manager.thread(parent_tid).trap_cx();

        /*
         * fork 在子进程返回 0。
         */
        child_trap_cx.set_syscall_ret(0);

        let child_fd_table = manager.process(parent_pid).fd_table.clone();
        let child_free_fds = manager.process(parent_pid).free_fds.clone();
        let child_cwd = manager.process(parent_pid).cwd.clone();

        let parent_tickets = manager.process(parent_pid).tickets;
        let parent_pass = manager.process(parent_pid).pass;

        let mut child_process = ProcessControlBlock::fork_from(
            child_pid,
            parent_pid,
            child_user_space,
            child_fd_table,
            child_free_fds,
            child_cwd,
            parent_tickets,
            parent_pass,
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

    child_pid as isize

}

pub fn exit_current_and_run_next(exit_code: i32) -> ! {
    let current_tid = processor::current_tid();

    let task_cx_ptr = {
        let mut manager = TASK_MANAGER.lock();

        let current_pid = manager.pid_of_tid(current_tid);

        log::info!(
            "[task] pid={} tid={} exited with code {}",
            current_pid,
            current_tid,
            exit_code,
        );

        manager.mark_thread_zombie(current_tid, exit_code);
        manager.wake_parent_waiting_for(current_pid);

        manager.thread_cx_ptr(current_tid)
    };

    let idle_cx_ptr = processor::idle_task_cx_ptr();

    unsafe {
        __switch(task_cx_ptr, idle_cx_ptr);
    }

    panic!("zombie thread returned after exit");
}

pub fn current_tid() -> Tid {
    processor::current_tid()
}

pub fn current_pid() -> Pid {
    let tid = processor::current_tid();
    let manager = TASK_MANAGER.lock();
    manager.pid_of_tid(tid)
}

pub fn current_task_id() -> usize {
    current_pid()
}

pub fn read_current_user_bytes(user_buf: usize, len: usize) -> Option<Vec<u8>> {
    let manager = TASK_MANAGER.lock();
    manager.read_current_user_bytes(user_buf, len)
}

pub fn write_current_user_bytes(user_buf: usize, data: &[u8]) -> Option<usize> {
    let pid = current_pid();
    let manager = TASK_MANAGER.lock();
    manager.write_user_bytes_by_pid(pid, user_buf, data)
}

pub fn wake_sleeping_tasks() {
    let now = crate::timer::ticks();
    let mut manager = TASK_MANAGER.lock();
    manager.wake_sleeping_threads(now);
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

    let file = match crate::fs::open(path) {
        Some(file) => file,
        None => {
            log::warn!("[exec] open failed: {}", path);
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
        crate::mm::MemorySet::new_user_test(&app_data);

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
        let mut manager = TASK_MANAGER.lock();
        let current_pid = manager.pid_of_tid(current_tid);

        if manager.process(current_pid).threads.len() != 1 {
            log::warn!(
                "[exec] pid={} has multiple threads, exec denied in first version",
                current_pid,
            );
            return -1;
        }

        let old_space = {
            let process = manager.process_mut(current_pid);
            process.close_non_standard_fds_on_exec();
            core::mem::replace(&mut process.user_space, new_user_space)
        };

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
    let manager = TASK_MANAGER.lock();
    manager.get_file(pid, fd)
}

pub fn alloc_fd_current(file: crate::fs::FileRef) -> isize {
    let pid = current_pid();
    let mut manager = TASK_MANAGER.lock();
    manager.alloc_fd(pid, file)
}

pub fn close_fd_current(fd: usize) -> isize {
    let pid = current_pid();
    let mut manager = TASK_MANAGER.lock();
    manager.close_fd(pid, fd)
}

pub fn current_cwd() -> String {
    let pid = current_pid();
    let manager = TASK_MANAGER.lock();

    manager.process(pid).cwd.clone()
}

pub fn set_current_cwd(new_cwd: String) -> isize {
    let pid = current_pid();
    let mut manager = TASK_MANAGER.lock();

    manager.process_mut(pid).cwd = new_cwd;

    0
}
