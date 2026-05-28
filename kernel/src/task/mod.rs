use alloc::boxed::Box;

use crate::mm::MemorySet;
use crate::trap::TrapContext;

unsafe extern "C" {
    fn __restore_user(cx: *const TrapContext) -> !;
}

/// 一次性进入用户态。
///
/// 第一版不返回：
/// - user_space 被 leak，保证页表和 Framed 页不被 drop
/// - trap_cx 被 leak，作为 restore_user 的上下文
pub fn run_user(user_space: MemorySet, trap_cx: TrapContext) -> ! {
    let root = user_space.root_ppn();

    let _user_space = Box::leak(Box::new(user_space));
    let trap_cx = Box::leak(Box::new(trap_cx));

    log::info!(
        "[task] enter user: entry={:#x}, sp={:#x}, root={:?}",
        trap_cx.user_pc(),
        trap_cx.user_sp(),
        root,
    );

    crate::mm::activate_page_table(root);

    unsafe {
        __restore_user(trap_cx as *const TrapContext);
    }
}