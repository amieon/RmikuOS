use alloc::string::String;
use alloc::vec::Vec;

use crate::mm::{MemorySet, PhysPageNum, VirtAddr, PAGE_SIZE_BITS};
use crate::mm::config::PAGE_SIZE;
use crate::sync::spin::Mutex;
use crate::task::thread;
use crate::trap::TrapContext;

use super::context::TaskContext;
use super::process::{Pid, ProcessControlBlock};
use super::processor;
use super::switch::__switch;
use super::thread::{BlockReason, ThreadControlBlock, ThreadStatus, Tid};

unsafe extern "C" {
    fn __restore_user(cx: *const TrapContext) -> !;
}


pub struct TaskManager {
    processes: Vec<Option<ProcessControlBlock>>,
    threads: Vec<Option<ThreadControlBlock>>,

    sched_alpha: isize,

    scale_cache: Vec<usize>,
    cache_alpha: isize,

    /// 睡眠唤醒早退缓存:所有睡眠线程里最早的到期时刻。
    /// None = 没有睡眠线程(或尚未有线程睡过)。
    next_wake_tick: Option<usize>,
}

pub enum WaitPidAction {
    Return(isize),
    Block,
}

pub enum JoinAction {
    Return(isize),
    Block,
}

impl TaskManager {
    pub const fn new() -> Self {
        Self {
            processes: Vec::new(),
            threads: Vec::new(),

            sched_alpha: 50,

            scale_cache: Vec::new(),
            cache_alpha: -1, // 哨兵：任何合法 alpha(0..=100) 都不等于它
            next_wake_tick: None,
        }
    }
    pub fn alloc_pid(&mut self) -> Pid {
        // 扫描即真源:空槽自然回收,不再维护 free_pids 第二本账。
        for pid in 0..self.processes.len() {
            if self.processes[pid].is_none() {
                return pid;
            }
        }

        self.processes.len()
    }

    pub fn alloc_tid(&mut self) -> Tid {
        for tid in 0..self.threads.len() {
            if self.threads[tid].is_none() {
                return tid;
            }
        }

        self.threads.len()
    }

    pub fn insert_process(&mut self, process: ProcessControlBlock) {
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

    pub fn insert_thread(&mut self, thread: ThreadControlBlock) {
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

    pub fn process_mut(&mut self, pid: Pid) -> &mut ProcessControlBlock {
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

    pub fn thread_mut(&mut self, tid: Tid) -> &mut ThreadControlBlock {
        self.threads
            .get_mut(tid)
            .and_then(|slot| slot.as_mut())
            .expect("[task] invalid tid")
    }

    pub fn try_process(&self, pid: Pid) -> Option<&ProcessControlBlock> {
        self.processes.get(pid)?.as_ref()
    }

    pub fn try_thread(&self, tid: Tid) -> Option<&ThreadControlBlock> {
        self.threads.get(tid)?.as_ref()
    }

    pub fn try_process_mut(&mut self, pid: Pid) -> Option<&mut ProcessControlBlock> {
        self.processes.get_mut(pid)?.as_mut()
    }

    pub fn try_thread_mut(&mut self, tid: Tid) -> Option<&mut ThreadControlBlock> {
        self.threads.get_mut(tid)?.as_mut()
    }

    pub fn pid_of_tid(&self, tid: Tid) -> Pid {
        self.thread(tid).pid
    }

    pub fn current_pid(&self) -> Pid {
        let tid = processor::current_tid();
        self.pid_of_tid(tid)
    }

    pub fn enqueue_ready_thread(&mut self, tid: Tid) {
        let pid = self.thread(tid).pid;

        if self.thread(tid).status != ThreadStatus::Ready {
            return;
        }

        if !self.process(pid).ready_threads.contains(&tid) {
            self.process_mut(pid).ready_threads.push(tid);
        }
    }



    pub fn count_ready_threads_in_process(&self, pid: Pid) -> usize {
        let Some(process) = self.processes.get(pid).and_then(|x| x.as_ref()) else {
            return 0;
        };

        process
            .ready_threads
            .iter()
            .filter(|&&tid| {
                self.threads
                    .get(tid)
                    .and_then(|x| x.as_ref())
                    .map(|thread| thread.status == ThreadStatus::Ready)
                    .unwrap_or(false)
            })
            .count()
    }

    pub fn set_sched_alpha(&mut self, alpha: isize) -> isize {
        if alpha < 0 || alpha > 100 {
            return -1;
        }
        self.sched_alpha = alpha;
        0
    }

    pub fn get_sched_alpha(&self) -> isize{
        self.sched_alpha
    }


    /// 取当前 alpha 下、runnable=n 的缩放因子，带缓存。
    /// alpha 变了就为新 alpha 重算已见过的格子；n 超界就扩容补算。
    fn scale_factor_cached(&mut self, n: usize) -> usize {
        let alpha = self.sched_alpha;

        // alpha 变化：整张已有表按新 alpha 重算（只重算见过的长度，通常很短）。
        if self.cache_alpha != alpha {
            for slot_n in 0..self.scale_cache.len() {
                self.scale_cache[slot_n] =
                    crate::math::sched_thread_scale(slot_n, alpha);
            }
            self.cache_alpha = alpha;
        }

        // n 超出当前表长：扩容并补算新格（用当前 alpha）。
        if n >= self.scale_cache.len() {
            for slot_n in self.scale_cache.len()..=n {
                let f = crate::math::sched_thread_scale(slot_n, alpha);
                self.scale_cache.push(f);
            }
        }

        self.scale_cache[n]
    }

    pub fn update_process_stride_by_alpha(&mut self, pid: Pid) {
        // runnable_count 由状态变迁点增量维护(block/wake/ready/exit/reap/create),
        // pick 热路径不再全表重扫。
        let runnable_threads = self.process(pid).runnable_count;

        if runnable_threads == 0 {
            let process = self.process_mut(pid);
            process.ready_thread_count_snapshot = 0;
            process.effective_tickets = 0;
            return;
        }

        // 原来：let factor = crate::math::sched_thread_scale(runnable_threads, alpha);
        let factor = self.scale_factor_cached(runnable_threads);

        let base_tickets = self.process(pid).tickets.max(1);

        let effective_tickets = base_tickets
            .saturating_mul(factor)
            .max(1);

        let new_stride =
            crate::task::process::stride_from_tickets(effective_tickets);

        let process = self.process_mut(pid);

        process.ready_thread_count_snapshot = runnable_threads;
        process.effective_tickets = effective_tickets;
        process.stride = new_stride;
    }

    pub fn pick_ready_process_by_stride(&mut self) -> Option<Pid> {
        let mut best: Option<(Pid, usize)> = None;

        for pid in 0..self.processes.len() {
            let Some(_) = self.processes[pid].as_ref() else {
                continue;
            };

            // 快速跳过没有任何可运行线程的进程(缓存值,O(1),借用即放)
            if self.processes[pid]
                .as_ref()
                .map_or(true, |p| p.runnable_count == 0)
            {
                continue;
            }

            if !self.process_has_ready_thread(pid) {
                continue;
            }

            //effective_tickets = tickets * sqrt(ready_threads)
            self.update_process_stride_by_alpha(pid);

            let process = self.process(pid);

            match best {
                None => {
                    best = Some((pid, process.pass));
                }
                Some((_, best_pass)) if process.pass < best_pass => {
                    best = Some((pid, process.pass));
                }
                _ => {}
            }
        }

        best.map(|(pid, _)| pid)
    }


    pub fn find_next_ready_thread(&mut self) -> Option<Tid> {
        let pid = self.pick_ready_process_by_stride()?;
        let tid = self.pick_ready_thread_in_process(pid)?;

        let process = self.process_mut(pid);
        process.pass = process.pass.wrapping_add(process.stride);

        Some(tid)
    }

    pub fn process_has_ready_thread(&self, pid: Pid) -> bool {
        let Some(process) = self.processes.get(pid).and_then(|x| x.as_ref()) else {
            return false;
        };

        process.ready_threads.iter().any(|&tid| {
            self.threads
                .get(tid)
                .and_then(|x| x.as_ref())
                .map(|thread| thread.status == ThreadStatus::Ready)
                .unwrap_or(false)
        })
    }
    

    pub fn pick_ready_thread_in_process(&mut self, pid: Pid) -> Option<Tid> {
        // 第一遍只读扫描找 pass 最小的 Ready 线程。
        // 不再 clone 整个 ready_threads Vec——旧实现每次 pick 都在
        // 内核堆里分配一次,而本函数处于全局调度锁内的热路径上。
        let mut best_tid: Option<Tid> = None;
        let mut best_pass: usize = usize::MAX;

        {
            let process = self.process(pid);
            for &tid in process.ready_threads.iter() {
                let Some(thread) = self.try_thread(tid) else {
                    continue;
                };

                if thread.status != ThreadStatus::Ready {
                    continue;
                }

                if best_tid.is_none() || thread.pass < best_pass {
                    best_tid = Some(tid);
                    best_pass = thread.pass;
                }
            }
        }

        let tid = best_tid?;

        {
            let process = self.process_mut(pid);

            if let Some(pos) = process.ready_threads.iter().position(|&x| x == tid) {
                process.ready_threads.remove(pos);
            } else {
                return None;
            }
        }

        {
            let thread = self.thread_mut(tid);
            thread.pass = thread.pass.wrapping_add(thread.stride);
        }

        Some(tid)
    }

    pub fn mark_thread_ready(&mut self, tid: Tid) -> bool {
        let pid = self.thread(tid).pid;
        let was_blocking;
        let should_enqueue = {
            let thread = self.thread_mut(tid);
            was_blocking = thread.status == ThreadStatus::Blocking;

            match thread.status {
                ThreadStatus::Blocking => {
                    thread.status = ThreadStatus::Ready;
                    thread.block_reason = BlockReason::None;
                    thread.running_on.is_none()
                }

                ThreadStatus::Running => {
                    if thread.running_on.is_none() {
                        thread.status = ThreadStatus::Ready;
                        thread.block_reason = BlockReason::None;
                        true
                    } else {
                        false
                    }
                }

                ThreadStatus::Ready => false,
                ThreadStatus::Zombie | ThreadStatus::Dead => false,
            }
        };

        // Blocking -> Ready:该线程重新变得可运行(Running->Ready 不变)
        if was_blocking {
            self.process_mut(pid).runnable_count += 1;
        }

        if should_enqueue {
            self.enqueue_ready_thread(tid);
            true
        } else {
            false
        }
    }

    pub fn mark_thread_zombie(&mut self, tid: Tid, exit_code: i32) {
        let pid = self.thread(tid).pid;

        let was_runnable = {
            let thread = self.thread_mut(tid);
            let r = matches!(
                thread.status,
                ThreadStatus::Ready | ThreadStatus::Running
            );
            thread.status = ThreadStatus::Zombie;
            thread.block_reason = BlockReason::None;
            thread.exit_code = exit_code;
            r
        };

        let process = self.process_mut(pid);
        if was_runnable {
            process.runnable_count = process.runnable_count.saturating_sub(1);
        }
        process.exit_code = exit_code;
    }

    pub fn thread_cx_ptr(&mut self, tid: Tid) -> *mut TaskContext {
        self.thread_mut(tid).task_cx_ptr()
    }

    pub fn prepare_thread(
        &mut self,
        tid: Tid,
    ) -> (Pid, PhysPageNum, usize, usize, *mut TaskContext) {
        let hart = crate::task::processor::current_hart_id();

        let pid = self.thread(tid).pid;
        let root = self.process(pid).user_space.root_ppn();

        let thread = self.thread_mut(tid);

        if thread.status != ThreadStatus::Ready {
            panic!(
                "[sched] prepare non-ready tid={} status={:?} hart={}",
                tid,
                thread.status,
                hart,
            );
        }

        if let Some(old_hart) = thread.running_on {
            panic!(
                "[sched] tid {} already running on hart {}, current hart {}",
                tid,
                old_hart,
                hart,
            );
        }

        thread.kernel_stack.check_guard();
        thread.running_on = Some(hart);
        thread.status = ThreadStatus::Running;

        (
            pid,
            root,
            thread.kernel_stack.top(),
            thread.trap_cx_addr,
            thread.task_cx_ptr(),
        )
    }

    pub fn block_thread(&mut self, tid: Tid, reason: BlockReason) {
        let pid = self.thread(tid).pid;

        let sleep_tick = match reason {
            BlockReason::Sleep { wake_tick } => Some(wake_tick),
            _ => None,
        };

        let was_runnable = {
            let thread = self.thread_mut(tid);
            let r = matches!(
                thread.status,
                ThreadStatus::Ready | ThreadStatus::Running
            );
            thread.status = ThreadStatus::Blocking;
            thread.block_reason = reason;
            r
        };

        if was_runnable {
            let process = self.process_mut(pid);
            process.runnable_count = process.runnable_count.saturating_sub(1);
        }

        // 睡眠早退缓存:记录最早的到期时刻
        if let Some(wake_tick) = sleep_tick {
            self.next_wake_tick = Some(match self.next_wake_tick {
                Some(t) => t.min(wake_tick),
                None => wake_tick,
            });
        }
    }

    pub fn wake_sleeping_threads(&mut self, now: usize) {
        // 早退:最早到期时刻还没到,本轮不必全表扫描
        if let Some(t) = self.next_wake_tick {
            if now < t {
                return;
            }
        }

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

            log::info!(
                "[task] wake thread {} from sleep: now={}, wake_tick={}",
                tid,
                now,
                wake_tick,
            );

            self.wake_blocked_thread(tid);
        }

        // 重建早退缓存:剩余睡眠者的最小到期时刻(含已死线程的残留
        // 也无妨——最多多扫一次,下轮重建即自清)
        let mut next: Option<usize> = None;
        for tid in 0..self.threads.len() {
            let Some(thread) = self.threads[tid].as_ref() else {
                continue;
            };
            if thread.status != ThreadStatus::Blocking {
                continue;
            }
            if let BlockReason::Sleep { wake_tick } = thread.block_reason {
                next = Some(match next {
                    Some(t) => t.min(wake_tick),
                    None => wake_tick,
                });
            }
        }
        self.next_wake_tick = next;
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

    pub fn write_user_bytes_by_pid(
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

    pub fn write_user_i32(&self, pid: Pid, user_ptr: usize, value: i32) -> Option<()> {
        if user_ptr == 0 {
            return Some(());
        }

        let bytes = value.to_ne_bytes();
        self.write_user_bytes_by_pid(pid, user_ptr, &bytes)?;
        Some(())
    }

    pub fn wake_blocked_thread(&mut self, tid: Tid) -> bool {
        let pid = self.thread(tid).pid;
        let should_enqueue = {
            let thread = self.thread_mut(tid);

            if thread.status != ThreadStatus::Blocking {
                return false;
            }

            thread.status = ThreadStatus::Ready;
            thread.block_reason = BlockReason::None;

            // 如果它还在某个 CPU 上，说明它还没真正 __switch 出去。
            // 不能现在入队，否则可能双核运行同一个 tid。
            thread.running_on.is_none()
        };

        // Blocking -> Ready:该线程重新变得可运行
        self.process_mut(pid).runnable_count += 1;

        if should_enqueue {
            self.enqueue_ready_thread(tid);
            true
        } else {
            false
        }
    }


    pub fn process_is_zombie(&self, pid: Pid) -> bool {
        let Some(process) = self.try_process(pid) else {
            return false;
        };

        if process.threads.is_empty() {
            return false;
        }

        process.threads.iter().all(|&tid| {
            let Some(thread) = self.try_thread(tid) else {
                return true;
            };

            matches!(thread.status, ThreadStatus::Zombie | ThreadStatus::Dead)
                && thread.running_on.is_none()
        })
    }

    pub fn reap_process(&mut self, pid: Pid) {
    log::info!(
        "[reap] pid={} root_ppn={:?}",
        pid,
        self.process(pid).user_space.root_ppn(),
    );
    // 先做防御检查
    {
        let Some(process) = self.try_process(pid) else {
            panic!("[task] reap non-existing process: pid={}", pid);
        };

        for &tid in process.threads.iter() {
            if let Some(thread) = self.try_thread(tid) {
                if thread.running_on.is_some() {
                    panic!(
                        "[task] reap process pid={} while tid={} running_on={:?}",
                        pid,
                        tid,
                        thread.running_on,
                    );
                }
            }
        }
    }

    let process = self
        .processes
        .get_mut(pid)
        .expect("[task] invalid reap pid")
        .take()
        .expect("[task] reap empty process slot");

    for tid in process.threads {
        if let Some(thread) = self.try_thread(tid) {
            if thread.running_on.is_some() {
                panic!(
                    "[task] reap tid={} while running_on={:?}",
                    tid,
                    thread.running_on,
                );
            }
        }

    }

}

pub fn wake_parent_waiting_for(&mut self, child_pid: Pid) -> bool {
    let parent_pid = match self.try_process(child_pid).and_then(|process| process.parent) {
        Some(parent_pid) => parent_pid,
        None => return false,
    };

    let parent_threads = match self.try_process(parent_pid) {
        Some(parent) => parent.threads.clone(),
        None => return false,
    };

    let mut need_ipi = false;

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
            if self.wake_blocked_thread(tid) {
                need_ipi = true;
            }

            log::info!(
                "[task] wake parent pid={} tid={} waiting for child {}",
                parent_pid,
                tid,
                child_pid,
            );
        }
    }

    need_ipi
}
    pub fn try_waitpid(
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

    pub fn get_file(&self, pid: Pid, fd: usize) -> Option<crate::fs::FileRef> {
        self.process(pid)
            .fd_table
            .get(fd)?
            .as_ref()
            .cloned()
    }

    pub fn alloc_fd(&mut self, pid: Pid, file: crate::fs::FileRef) -> isize {
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

    pub fn close_fd(&mut self, pid: Pid, fd: usize) -> isize {
        let file = {
            let process = self.process_mut(pid);
            if fd >= process.fd_table.len() {
                return -1;
            }
            let Some(file) = process.fd_table[fd].take() else {
                return -1;
            };
            process.free_fds.push(fd);
            file            
        }; 

        self.release_file(&file);
        0
    }

    pub fn min_thread_pass_in_process(&self, pid: Pid) -> usize {
        let process = self.process(pid);

        let mut min_pass = usize::MAX;

        for &tid in process.threads.iter() {
            let Some(thread) = self.try_thread(tid) else {
                continue;
            };

            if thread.status == ThreadStatus::Dead
                || thread.status == ThreadStatus::Zombie {
                continue;
            }

            min_pass = min_pass.min(thread.pass);
        }

        if min_pass == usize::MAX {
            0
        } else {
            min_pass
        }
    }

    pub fn create_thread_current(
        &mut self,
        entry: usize,
        arg0: usize,
        arg1: usize,
        user_stack_top: usize,
    ) -> isize {
        if entry == 0 || user_stack_top == 0 {
            return -1;
        }

        let current_tid = processor::current_tid();
        let pid = self.pid_of_tid(current_tid);

        let tid = self.alloc_tid();
        let user_stack_top = user_stack_top & !0xf;

        let mut thread = ThreadControlBlock::new_user_thread(
            tid,
            pid,
            entry,
            user_stack_top,
            arg0,
            arg1,
        );
        let init_pass = self.min_thread_pass_in_process(pid);
        thread.pass = init_pass;

        self.insert_thread(thread);

        {
            let process = self.process_mut(pid);
            process.threads.push(tid);
            process.ready_threads.push(tid);
            process.runnable_count += 1;
        }

        log::info!(
            "[thread] create: pid={} tid={} entry={:#x} arg0={:#x} arg1={:#x} user_sp={:#x}",
            pid,
            tid,
            entry,
            arg0,
            arg1,
            user_stack_top,
        );

        tid as isize
    }


    pub fn wake_threads_joining(&mut self, target_tid: Tid) -> bool {
        let target_pid = match self.try_thread(target_tid) {
            Some(thread) => thread.pid,
            None => return false,
        };

        let tids = match self.try_process(target_pid) {
            Some(process) => process.threads.clone(),
            None => return false,
        };

        let mut need_ipi = false;

        for tid in tids {
            let should_wake = match self.try_thread(tid) {
                Some(thread) if thread.status == ThreadStatus::Blocking => {
                    match thread.block_reason {
                        BlockReason::Join { tid } => tid == target_tid,
                        _ => false,
                    }
                }
                _ => false,
            };

            if should_wake {
                if self.wake_blocked_thread(tid) {
                    need_ipi = true;
                }
            }
        }

        need_ipi
    }

    pub fn dump_tasks(&self) {
        log::warn!("===== TASK DUMP =====");

        for pid in 0..self.processes.len() {
            let Some(process) = self.processes[pid].as_ref() else {
                continue;
            };

            log::warn!(
                "pid={} children={:?} ready_threads={:?}",
                pid,
                process.children,
                process.ready_threads,
            );

            for &tid in process.threads.iter() {
                if let Some(thread) = self.try_thread(tid) {
                    log::warn!(
                        "  tid={} status={:?} block={:?} running_on={:?} run_ticks={}",
                        tid,
                        thread.status,
                        thread.block_reason,
                        thread.running_on,
                        thread.run_ticks,
                    );
                }
            }
        }

        log::warn!("=====================");
    }


    pub fn reap_thread(&mut self, tid: Tid) {
    {
        let Some(thread) = self.try_thread(tid) else {
            panic!("[thread] reap non-existing thread: tid={}", tid);
        };

        if thread.running_on.is_some() {
            panic!(
                "[thread] reap running thread tid={} running_on={:?}",
                tid,
                thread.running_on,
            );
        }
    }

    let thread = self
        .threads
        .get_mut(tid)
        .and_then(|slot| slot.take())
        .expect("[thread] reap empty thread slot");

    let pid = thread.pid;

    if let Some(process) = self.try_process_mut(pid) {
        process.threads.retain(|&x| x != tid);
        process.ready_threads.retain(|&x| x != tid);
        if matches!(
            thread.status,
            ThreadStatus::Ready | ThreadStatus::Running
        ) {
            process.runnable_count = process.runnable_count.saturating_sub(1);
        }
    }


    log::info!(
        "[thread] reaped tid={} from pid={}",
        tid,
        pid,
    );
}

    
}


impl TaskManager {
    pub fn count_runnable_threads_in_process(&self, pid: Pid) -> usize {
        let Some(process) = self.processes.get(pid).and_then(|x| x.as_ref()) else {
            return 0;
        };

        let mut count = 0;

        for &tid in process.threads.iter() {
            let Some(thread) = self.try_thread(tid) else {
                continue;
            };

            if thread.status == ThreadStatus::Ready
                || thread.status == ThreadStatus::Running
            {
                count += 1;
            }
        }

        count
    }

    pub fn count_sched_runnable_threads_in_process(&self, pid: Pid) -> usize {
        let Some(process) = self.processes.get(pid).and_then(|x| x.as_ref()) else {
            return 0;
        };

        let mut count = 0;

        for &tid in process.ready_threads.iter() {
            let Some(thread) = self.try_thread(tid) else {
                continue;
            };

            if thread.status == ThreadStatus::Ready {
                count += 1;
            }
        }

        if let Some(current_tid) = crate::task::processor::current_tid_opt() {
            if self.pid_of_tid(current_tid) == pid {
                let thread = self.thread(current_tid);

                if thread.status == ThreadStatus::Running {
                    count += 1;
                }
            }
        }

        count
    }


    pub fn reset_sched_stat(&mut self) -> isize {
        for process in self.processes.iter_mut() {
            if let Some(process) = process.as_mut() {
                process.run_ticks = 0;
                process.ready_thread_count_snapshot = 0;
                process.pass = 0;
                // process.effective_tickets = 建议 effective_tickets 保留，不要清 process.tickets;
            }
        }

        for thread in self.threads.iter_mut() {
            if let Some(thread) = thread.as_mut() {
                thread.run_ticks = 0;
            }
        }
        0
    }

    pub fn wake_threads_by_reason(&mut self, reason: BlockReason) -> isize {
        let mut wake_list = Vec::new();

        for tid in 0..self.threads.len() {
            let Some(thread) = self.threads[tid].as_ref() else {
                continue;
            };

            if thread.status == ThreadStatus::Blocking && thread.block_reason == reason {
                wake_list.push(tid);
            }
        }

        for tid in wake_list {
            self.wake_blocked_thread(tid);
        }

        0
    }

    pub fn release_file(&mut self, file: &crate::fs::file::FileRef) {
        match file.on_close_kind() {
            crate::fs::file::PipeCloseKind::WriterGone => {
                self.wake_threads_by_reason(BlockReason::PipeRead);
            }
            crate::fs::file::PipeCloseKind::ReaderGone => {
                self.wake_threads_by_reason(BlockReason::PipeWrite);
            }
            crate::fs::file::PipeCloseKind::Nothing => {}
        }
    }


    pub fn has_ready_thread(&self) -> bool {
        for pid in 0..self.processes.len() {
            if self.process_has_ready_thread(pid) {
                return true;
            }
        }
        false
    }
    pub fn has_running_thread(&self) -> bool {
        for slot in self.threads.iter() {
            if let Some(thread) = slot {
                if thread.status == ThreadStatus::Running {
                    return true;
                }
            }
        }
        false
    }
}