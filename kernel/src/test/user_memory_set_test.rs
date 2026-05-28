use crate::mm::{MemorySet,user_layout::*,address::*};

pub fn user_memory_set_test() {
    /*
     * 这里先随便放几个字节。
     * 之后换成真正的用户程序机器码。
     */
    let app: &[u8] = &[0u8; 16];

    let (user_space, entry, user_sp) = MemorySet::new_user_test(app);

    assert_eq!(entry, USER_TEXT_BASE);
    assert_eq!(user_sp, USER_STACK_TOP);

    let text_pte = user_space
        .translate(VirtAddr(USER_TEXT_BASE).floor())
        .expect("user text is not mapped");

    let stack_pte = user_space
        .translate(VirtAddr(USER_STACK_TOP - 1).floor())
        .expect("user stack is not mapped");

    let kernel_va = crate::mm::kernel_phys_to_virt(crate::arch::MEMORY_START);
    let kernel_pte = user_space
        .translate(VirtAddr(kernel_va).floor())
        .expect("kernel mapping is not mapped in user page table");

    log::info!(
        "[mm] user MemorySet test passed: entry={:#x}, sp={:#x}, text_ppn={:?}, stack_ppn={:?}, kernel_ppn={:?}",
        entry,
        user_sp,
        text_pte.ppn(),
        stack_pte.ppn(),
        kernel_pte.ppn(),
    );
}