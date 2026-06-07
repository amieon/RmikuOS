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


pub struct TaskManager {
    processes: Vec<Option<ProcessControlBlock>>,
    threads: Vec<Option<ThreadControlBlock>>,

    free_pids: Vec<Pid>,
    free_tids: Vec<Tid>,

    
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

            free_pids: Vec::new(),
            free_tids: Vec::new(),
        }
    }

    pub fn alloc_pid(&mut self) -> Pid {
        self.free_pids.pop().unwrap_or(self.processes.len())
    }

    pub fn alloc_tid(&mut self) -> Tid {
        self.free_tids.pop().unwrap_or(self.threads.len())
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


    pub fn update_process_stride_by_sqrt(&mut self, pid: Pid) {
        let ready_threads = self.count_ready_threads_in_process(pid);

        if ready_threads == 0 {
            return;
        }

        let factor = crate::math::isqrt(ready_threads).max(1);

        let base_tickets = self.process(pid).tickets.max(1);

        let effective_tickets = base_tickets
            .saturating_mul(factor)
            .max(1);

        let new_stride = crate::task::process::stride_from_tickets(effective_tickets);

        self.process_mut(pid).stride = new_stride;
    }

    pub fn pick_ready_process_by_stride(&mut self) -> Option<Pid> {
        let mut best: Option<(Pid, usize)> = None;

        for pid in 0..self.processes.len() {
            let Some(_) = self.processes[pid].as_ref() else {
                continue;
            };

            if !self.process_has_ready_thread(pid) {
                continue;
            }

            //effective_tickets = tickets * sqrt(ready_threads)
            self.update_process_stride_by_sqrt(pid);

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
        let ready_threads = {
            let process = self.process(pid);
            process.ready_threads.clone()
        };

        let mut best_tid: Option<Tid> = None;
        let mut best_pass: usize = usize::MAX;

        for tid in ready_threads {
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

    pub fn mark_thread_ready(&mut self, tid: Tid) {
        {
            let thread = self.thread_mut(tid);

            if thread.status == ThreadStatus::Running {
                thread.status = ThreadStatus::Ready;
            }
        }

        self.enqueue_ready_thread(tid);
    }

    pub fn mark_thread_zombie(&mut self, tid: Tid, exit_code: i32) {
        let pid = self.thread(tid).pid;

        {
            let thread = self.thread_mut(tid);
            thread.status = ThreadStatus::Zombie;
            thread.block_reason = BlockReason::None;
            thread.exit_code = exit_code;
        }

        self.process_mut(pid).exit_code = exit_code;
    }

    pub fn thread_cx_ptr(&mut self, tid: Tid) -> *mut TaskContext {
        self.thread_mut(tid).task_cx_ptr()
    }

    pub fn prepare_thread(
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

    pub fn block_thread(&mut self, tid: Tid, reason: BlockReason) {
        let thread = self.thread_mut(tid);
        thread.status = ThreadStatus::Blocking;
        thread.block_reason = reason;
    }

    pub fn wake_sleeping_threads(&mut self, now: usize) {
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

    pub fn process_is_zombie(&self, pid: Pid) -> bool {
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

    pub fn reap_process(&mut self, pid: Pid) {
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

    pub fn wake_parent_waiting_for(&mut self, child_pid: Pid) {
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


    pub fn wake_threads_joining(&mut self, target_tid: Tid) {
        let target_pid = match self.try_thread(target_tid) {
            Some(thread) => thread.pid,
            None => return,
        };

        let tids = match self.try_process(target_pid) {
            Some(process) => process.threads.clone(),
            None => return,
        };

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
                {
                    let thread = self.thread_mut(tid);
                    thread.status = ThreadStatus::Ready;
                    thread.block_reason = BlockReason::None;
                }

                self.enqueue_ready_thread(tid);

                log::info!(
                    "[thread] wake tid={} joining tid={}",
                    tid,
                    target_tid,
                );
            }
        }
    }


    pub fn reap_thread(&mut self, tid: Tid) {
        let Some(thread) = self.threads.get_mut(tid).and_then(|slot| slot.take()) else {
            panic!("[thread] reap empty thread slot: tid={}", tid);
        };

        let pid = thread.pid;

        if let Some(process) = self.try_process_mut(pid) {
            process.threads.retain(|&x| x != tid);
            process.ready_threads.retain(|&x| x != tid);
        }

        self.free_tids.push(tid);

        log::info!(
            "[thread] reaped tid={} from pid={}",
            tid,
            pid,
        );
    }

    
}


