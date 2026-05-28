use alloc::boxed::Box;

use crate::mm::MemorySet;
use crate::trap::TrapContext;

const KERNEL_STACK_SIZE: usize = 16 * 1024;
const TRAP_CONTEXT_SIZE: usize = core::mem::size_of::<TrapContext>();

#[repr(align(16))]
pub struct KernelStack {
    data: [u8; KERNEL_STACK_SIZE],
}

impl KernelStack {
    pub const fn new() -> Self {
        Self {
            data: [0; KERNEL_STACK_SIZE],
        }
    }

    pub fn top(&self) -> usize {
        self.data.as_ptr() as usize + KERNEL_STACK_SIZE
    }

    pub unsafe fn push_context(&self, cx: TrapContext) -> *mut TrapContext {
        let cx_ptr = (self.top() - TRAP_CONTEXT_SIZE) as *mut TrapContext;
        cx_ptr.write(cx);
        cx_ptr
    }
}

static USER_KERNEL_STACK: KernelStack = KernelStack::new();

unsafe extern "C" {
    fn __restore_user(cx: *const TrapContext) -> !;
}

pub fn run_user(user_space: MemorySet, trap_cx: TrapContext) -> ! {
    let root = user_space.root_ppn();


    let entry = trap_cx.user_pc();
    let user_sp = trap_cx.user_sp();


    let pte = user_space
        .translate(crate::mm::VirtAddr(entry).floor())
        .expect("user entry not mapped");

    log::info!(
        "[task] user entry pte: entry={:#x}, ppn={:?}, bits={:#x}",
        entry,
        pte.ppn(),
        pte.bits(),
    );


    let _user_space = Box::leak(Box::new(user_space));


    let trap_cx_ptr = unsafe {
        USER_KERNEL_STACK.push_context(trap_cx)
    };

    log::info!(
        "[task] enter user: entry={:#x}, sp={:#x}, root={:?}, kstack_top={:#x}, trap_cx={:#x}",
        entry,
        user_sp,
        root,
        USER_KERNEL_STACK.top(),
        trap_cx_ptr as usize,
    );

    crate::mm::activate_page_table(root);

    unsafe {
        __restore_user(trap_cx_ptr as *const TrapContext);
    }
}