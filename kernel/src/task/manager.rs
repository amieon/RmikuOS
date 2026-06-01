use alloc::vec::Vec;

use crate::mm::{MemorySet,PhysPageNum, VirtAddr, PAGE_SIZE_BITS};
use crate::mm::config::PAGE_SIZE;
use crate::sync::spin::Mutex;
use crate::trap::TrapContext;

use super::context::TaskContext;
use super::processor;
use super::switch::__switch;
use super::task::{TaskControlBlock, TaskStatus, BlockReason};

unsafe extern "C" {
    fn __restore_user(cx: *const TrapContext) -> !;
}

pub struct TaskManager {
    tasks: Vec<TaskControlBlock>,
}

enum WaitPidAction {
    Return(isize),
    Block,
}

impl TaskManager {
    pub const fn new() -> Self {
        Self { tasks: Vec::new() }
    }

    pub fn add_task(&mut self, task: TaskControlBlock) {
        self.tasks.push(task);
    }

    fn find_next_ready(&self) -> Option<usize> {
        for id in 0..self.tasks.len() {
            if self.tasks[id].status == TaskStatus::Ready {
                return Some(id);
            }
        }
        None
    }

    fn mark_current_ready(&mut self, id: usize) {
        if self.tasks[id].status == TaskStatus::Running {
            self.tasks[id].status = TaskStatus::Ready;
        }
    }

    fn mark_current_zombie(&mut self, id: usize, exit_code: i32) {
        self.tasks[id].status = TaskStatus::Zombie;
        self.tasks[id].exit_code = exit_code;
    }

    fn task_cx_ptr(&mut self, id: usize) -> *mut TaskContext {
        self.tasks[id].task_cx_ptr()
    }

    fn prepare_task(&mut self, id: usize) -> (PhysPageNum, usize, usize, *mut TaskContext) {
        self.tasks[id].status = TaskStatus::Running;

        let task = &mut self.tasks[id];

        (
            task.root_ppn(),
            task.kernel_stack.top(),
            task.trap_cx_addr,
            task.task_cx_ptr(),
        )
    }

    pub fn read_current_user_bytes(&self, user_buf: usize, len: usize) -> Option<Vec<u8>> {
        let current = processor::current_task_id();
        let task = self.tasks.get(current)?;

        let mut bytes = Vec::new();

        for offset in 0..len {
            let va = user_buf.checked_add(offset)?;
            let vpn = VirtAddr(va).floor();
            let page_offset = va & (PAGE_SIZE - 1);

            let pte = task.user_space.translate(vpn)?;

            let pa = (pte.ppn().0 << PAGE_SIZE_BITS) + page_offset;
            let kva = crate::mm::kernel_phys_to_virt(pa);

            let byte = unsafe { core::ptr::read_volatile(kva as *const u8) };
            bytes.push(byte);
        }

        Some(bytes)
    }

    fn write_current_user_bytes(&self, task_id: usize, user_buf: usize, data: &[u8]) -> Option<usize> {
        for (offset, byte) in data.iter().enumerate() {
            let va = user_buf.checked_add(offset)?;
            let vpn = VirtAddr(va).floor();
            let page_offset = va & (PAGE_SIZE - 1);

            let pte = self.tasks[task_id].user_space.translate(vpn)?;

            let pa = (pte.ppn().0 << PAGE_SIZE_BITS) + page_offset;
            let kva = crate::mm::kernel_phys_to_virt(pa);

            unsafe {
                core::ptr::write_volatile(kva as *mut u8, *byte);
            }
        }

        Some(data.len())
    }

    fn has_alive_tasks(&self) -> bool {
        self.tasks.iter().any(|task| {
            matches!(
                task.status,
                TaskStatus::Ready | TaskStatus::Running | TaskStatus::Blocking | TaskStatus::Zombie
            )
        })
    }

    fn block_current(&mut self, id: usize, reason: BlockReason) {
        self.tasks[id].status = TaskStatus::Blocking;
        self.tasks[id].block_reason = reason;
    }

    fn wake_parent_waiting_for(&mut self, child_pid: usize) {
        let parent = self.tasks[child_pid].parent;

        let Some(parent_pid) = parent else {
            return;
        };

        if self.tasks[parent_pid].status != TaskStatus::Blocking {
            return;
        }

        match self.tasks[parent_pid].block_reason {
            BlockReason::WaitPid { pid } => {
                let matched = pid == -1 || pid as usize == child_pid;

                if matched {
                    self.tasks[parent_pid].status = TaskStatus::Ready;
                    self.tasks[parent_pid].block_reason = BlockReason::None;

                    log::info!(
                        "[task] wake parent task {} waiting for child {}",
                        parent_pid,
                        child_pid,
                    );
                }
            }
            _ => {}
        }
    }
    fn write_user_i32(&self, task_id: usize, user_ptr: usize, value: i32) -> Option<()> {
        if user_ptr == 0 {
            return Some(());
        }

        let bytes = value.to_ne_bytes();

        for (offset, byte) in bytes.iter().enumerate() {
            let va = user_ptr.checked_add(offset)?;
            let vpn = VirtAddr(va).floor();
            let page_offset = va & (PAGE_SIZE - 1);

            let pte = self.tasks[task_id].user_space.translate(vpn)?;

            let pa = (pte.ppn().0 << PAGE_SIZE_BITS) + page_offset;
            let kva = crate::mm::kernel_phys_to_virt(pa);

            unsafe {
                core::ptr::write_volatile(kva as *mut u8, *byte);
            }
        }

        Some(())
    }


    fn try_waitpid(&mut self, current: usize, pid: isize, exit_code_ptr: usize) -> WaitPidAction {
        
        //没有子进程。
        if self.tasks[current].children.is_empty() {
            return WaitPidAction::Return(-1);
        }

        let mut has_matched_child = false;

        let children_snapshot = self.tasks[current].children.clone();

        for child_pid in children_snapshot {
            let matched = pid == -1 || pid as usize == child_pid;

            if !matched {
                continue;
            }

            has_matched_child = true;

            if self.tasks[child_pid].status == TaskStatus::Zombie {
                let code = self.tasks[child_pid].exit_code;

                if self.write_user_i32(current, exit_code_ptr, code).is_none() {
                    return WaitPidAction::Return(-1);
                }

                self.tasks[child_pid].status = TaskStatus::Dead;

                self.tasks[current]
                    .children
                    .retain(|&x| x != child_pid);

                log::info!(
                    "[task] task {} collected child {}, exit_code={}",
                    current,
                    child_pid,
                    code,
                );

                return WaitPidAction::Return(child_pid as isize);
            }
        }

        if !has_matched_child {
            return WaitPidAction::Return(-1);
        }

        WaitPidAction::Block
    }
    fn wake_sleeping_tasks(&mut self, now: usize) {
        for task in self.tasks.iter_mut() {
            if task.status != TaskStatus::Blocking {
                continue;
            }

            match task.block_reason {
                BlockReason::Sleep { wake_tick } if now >= wake_tick => {
                    log::info!(
                        "[task] wake task {} from sleep: now={}, wake_tick={}",
                        task.id,
                        now,
                        wake_tick,
                    );

                    task.status = TaskStatus::Ready;
                    task.block_reason = BlockReason::None;
                }
                _ => {}
            }
        }
    }



    fn get_file(&self, task_id: usize, fd: usize) -> Option<crate::fs::FileRef> {
        self.tasks
            .get(task_id)?
            .fd_table
            .get(fd)?
            .as_ref()
            .cloned()
    }

    fn alloc_fd(&mut self, task_id: usize, file: crate::fs::FileRef) -> isize {
        let fd_table = &mut self.tasks[task_id].fd_table;

        for i in 0..fd_table.len() {
            if fd_table[i].is_none() {
                fd_table[i] = Some(file);
                return i as isize;
            }
        }

        fd_table.push(Some(file));
        (fd_table.len() - 1) as isize
    }

    fn close_fd(&mut self, task_id: usize, fd: usize) -> isize {
        if fd >= self.tasks[task_id].fd_table.len() {
            return -1;
        }

        if self.tasks[task_id].fd_table[fd].is_none() {
            return -1;
        }

        self.tasks[task_id].fd_table[fd] = None;
        0
    }
}

pub fn sleep_current_and_run_next(ticks: usize) -> isize {
    if ticks == 0 {
        return 0;
    }

    let current = processor::current_task_id();
    let wake_tick = crate::timer::ticks() + ticks;

    let task_cx_ptr = {
        let mut manager = TASK_MANAGER.lock();

        log::info!(
            "[task] task {} sleep until tick {}",
            current,
            wake_tick,
        );

        manager.block_current(
            current,
            BlockReason::Sleep {
                wake_tick,
            },
        );

        manager.task_cx_ptr(current)
    };

    let idle_cx_ptr = processor::idle_task_cx_ptr();

    unsafe {
        __switch(task_cx_ptr, idle_cx_ptr);
    }

    0
}
pub fn waitpid_current(pid: isize, exit_code_ptr: usize) -> isize {
    let current = processor::current_task_id();

    loop {
        let action = {
            let mut manager = TASK_MANAGER.lock();
            manager.try_waitpid(current, pid, exit_code_ptr)
        };

        match action {
            WaitPidAction::Return(ret) => return ret,
            WaitPidAction::Block => {
                let task_cx_ptr = {
                    let mut manager = TASK_MANAGER.lock();

                    log::info!(
                        "[task] task {} waitpid(pid={}) blocking",
                        current,
                        pid,
                    );

                    manager.block_current(
                        current,
                        BlockReason::WaitPid {
                            pid,
                        },
                    );

                    manager.task_cx_ptr(current)
                };

                let idle_cx_ptr = processor::idle_task_cx_ptr();

                unsafe {
                    __switch(task_cx_ptr, idle_cx_ptr);
                }

                /*
                 * 被 child exit 唤醒后，会回到这里，然后 loop 重新检查 zombie child。
                 */
            }
        }
    }
}
pub fn fork_current() -> isize {
    let parent = processor::current_task_id();

    let child_pid = {
        let mut manager = TASK_MANAGER.lock();

        let child_pid = manager.tasks.len();


        let child_user_space =
            MemorySet::from_existed_user(&manager.tasks[parent].user_space);


        let mut child_trap_cx = *manager.tasks[parent].trap_cx();


        let child_fd_table = manager.tasks[parent].fd_table.clone();

        child_trap_cx.set_syscall_ret(0);


        let child = TaskControlBlock::fork_from(
            child_pid,
            parent,
            child_user_space,
            child_trap_cx,
            child_fd_table,
        );

        manager.tasks[parent].children.push(child_pid);
        manager.tasks.push(child);

        log::info!(
            "[task] fork: parent={} child={}",
            parent,
            child_pid,
        );

        child_pid
    };


    child_pid as isize
}

pub fn exit_current_and_run_next(exit_code: i32) -> ! {
    let current = processor::current_task_id();

    let task_cx_ptr = {
        let mut manager = TASK_MANAGER.lock();

        log::info!(
            "[task] task {} exited with code {}",
            current,
            exit_code,
        );

        manager.tasks[current].status = TaskStatus::Zombie;
        manager.tasks[current].exit_code = exit_code;
        manager.tasks[current].block_reason = BlockReason::None;

        manager.wake_parent_waiting_for(current);

        manager.task_cx_ptr(current)
    };

    let idle_cx_ptr = processor::idle_task_cx_ptr();

    unsafe {
        __switch(task_cx_ptr, idle_cx_ptr);
    }

    panic!("zombie task returned after exit");
}

static TASK_MANAGER: Mutex<TaskManager> = Mutex::new(TaskManager::new());

pub fn init() {
    let shell_app = crate::loader::find_app("shell")
        .or_else(|| crate::loader::find_app("00_shell"))
        .unwrap_or(0);

    let app = crate::loader::get_app_data(shell_app);
    let name = crate::loader::get_app_name(shell_app);

    log::info!(
        "[task] load init app: id={}, name={}, size={} bytes, first4=[{:02x}, {:02x}, {:02x}, {:02x}]",
        shell_app,
        name,
        app.len(),
        app.get(0).copied().unwrap_or(0),
        app.get(1).copied().unwrap_or(0),
        app.get(2).copied().unwrap_or(0),
        app.get(3).copied().unwrap_or(0),
    );

    let task = TaskControlBlock::new(0, app);

    {
        let mut manager = TASK_MANAGER.lock();
        manager.add_task(task);
    }

    log::info!("[task] loaded init shell");
}

pub fn run_first_task() -> ! {
    run_tasks()
}

pub fn run_tasks() -> ! {
    loop {
        let next = {
            let mut manager = TASK_MANAGER.lock();
            let now = crate::timer::ticks();
            manager.wake_sleeping_tasks(now);

            if let Some(id) = manager.find_next_ready() {
                let (root, kstack_top, trap_cx_addr, task_cx_ptr) = manager.prepare_task(id);

                log::info!(
                    "[task] schedule task {}: root={:?}, kstack_top={:#x}, trap_cx={:#x}",
                    id,
                    root,
                    kstack_top,
                    trap_cx_addr,
                );

                Some((id, root, task_cx_ptr))
            } else {
                None
            }
        };

        if let Some((id, root, task_cx_ptr)) = next {
            processor::set_current(Some(id));

            crate::mm::activate_page_table(root);

            let idle_cx_ptr = processor::idle_task_cx_ptr();

            unsafe {
                __switch(idle_cx_ptr, task_cx_ptr);
            }

            processor::set_current(None);
        } else {
            crate::arch::enable_interrupt();
            crate::arch::wait_for_interrupt();
            crate::arch::disable_interrupt();
        }
    }
}

#[no_mangle]
pub extern "C" fn __task_entry() -> ! {
    let current = processor::current_task_id();

    let trap_cx_addr = {
        let manager = TASK_MANAGER.lock();
        manager.tasks[current].trap_cx_addr
    };

    unsafe {
        __restore_user(trap_cx_addr as *const TrapContext);
    }
}

pub fn suspend_current_and_run_next() -> isize {
    let current = processor::current_task_id();

    let task_cx_ptr = {
        let mut manager = TASK_MANAGER.lock();
        manager.mark_current_ready(current);
        manager.task_cx_ptr(current)
    };

    let idle_cx_ptr = processor::idle_task_cx_ptr();

    unsafe {
        __switch(task_cx_ptr, idle_cx_ptr);
    }

    0
}

pub fn preempt_current_and_run_next() {
    let current = processor::current_task_id();

    let task_cx_ptr = {
        let mut manager = TASK_MANAGER.lock();
        manager.mark_current_ready(current);
        manager.task_cx_ptr(current)
    };

    let idle_cx_ptr = processor::idle_task_cx_ptr();

    unsafe {
        __switch(task_cx_ptr, idle_cx_ptr);
    }
}


pub fn current_task_id() -> usize {
    processor::current_task_id()
}

pub fn read_current_user_bytes(user_buf: usize, len: usize) -> Option<Vec<u8>> {
    let manager = TASK_MANAGER.lock();
    manager.read_current_user_bytes(user_buf, len)
}

pub fn write_current_user_bytes(user_buf: usize, data: &[u8]) -> Option<usize> {
    let current = processor::current_task_id();
    let manager = TASK_MANAGER.lock();
    manager.write_current_user_bytes(current, user_buf, data)
}


pub fn wake_sleeping_tasks() {
    let now = crate::timer::ticks();
    let mut manager = TASK_MANAGER.lock();
    manager.wake_sleeping_tasks(now);
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
    use alloc::string::String;
    use alloc::vec::Vec;

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

    /*
     * 如果传入 hello，就变成 /bin/hello。
     * 如果传入 /bin/hello，就直接使用。
     */
    let mut path_buf = String::new();

    let path = if name.starts_with('/') {
        name
    } else {
        path_buf.push_str("/bin/");
        path_buf.push_str(name);
        path_buf.as_str()
    };

    /*
     * 先从旧地址空间读取 argv。
     * 注意：必须在替换 user_space 之前读。
     */
    let argv = match read_exec_args(args_ptr, name) {
        Some(argv) => argv,
        None => return -1,
    };

    let argc = argv.len();

    /*
     * 从 VFS 打开程序文件。
     */
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

    /*
     * 读取完整 app binary。
     */
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

    /*
     * 创建新用户地址空间。
     */
    let (mut new_user_space, entry, user_sp) =
        crate::mm::MemorySet::new_user_test(&app_data);

    /*
     * 把 argc/argv 放进新用户栈。
     */
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

    /*
     * a0 = argc
     * a1 = argv
     *
     * 注意：trap handler 会把 sys_exec 的返回值写回 a0。
     * 所以 sys_exec 成功时要 return argc。
     */
    new_trap_cx.set_app_args(argc, argv_ptr);

    let current = processor::current_task_id();

    let new_root = {
        let mut manager = TASK_MANAGER.lock();
        let task = &mut manager.tasks[current];

        let old_space = core::mem::replace(&mut task.user_space, new_user_space);

        *task.trap_cx_mut() = new_trap_cx;

        let root = task.root_ppn();

        drop(old_space);

        root
    };

    crate::mm::activate_page_table(new_root);

    /*
     * 关键：成功 exec 返回 argc。
     * trap handler 会把这个值写到新 TrapContext 的 a0，
     * 正好作为 main(argc, argv) 的 argc。
     */
    argc as isize
}



fn read_exec_args(args_ptr: usize, default_argv0: &str) -> Option<alloc::vec::Vec<alloc::vec::Vec<u8>>> {
    use alloc::vec::Vec;

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

        /*
         * argv 字符串里不要带 NUL。后面压用户栈时我们自己补 '\0'。
         */
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
    use crate::mm::{
        VirtAddr,
        PAGE_SIZE_BITS,
        kernel_phys_to_virt,
    };
    use crate::mm::config::PAGE_SIZE;

    for (offset, byte) in data.iter().enumerate() {
        let va = user_va.checked_add(offset)?;
        let vpn = VirtAddr(va).floor();
        let page_offset = va & (PAGE_SIZE - 1);

        let pte = user_space.translate(vpn)?;
        let pa = (pte.ppn().0 << PAGE_SIZE_BITS) + page_offset;
        let kva = kernel_phys_to_virt(pa);

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
    argv: &[alloc::vec::Vec<u8>],
) -> Option<(usize, usize)> {
    use alloc::vec::Vec;

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
    let current = processor::current_task_id();
    let manager = TASK_MANAGER.lock();
    manager.get_file(current, fd)
}

pub fn alloc_fd_current(file: crate::fs::FileRef) -> isize {
    let current = processor::current_task_id();
    let mut manager = TASK_MANAGER.lock();
    manager.alloc_fd(current, file)
}

pub fn close_fd_current(fd: usize) -> isize {
    let current = processor::current_task_id();
    let mut manager = TASK_MANAGER.lock();
    manager.close_fd(current, fd)
}