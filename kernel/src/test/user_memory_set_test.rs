use crate::mm::{address::*, user_layout::*, MemorySet};

pub fn user_memory_set_test() {
    let app = crate::loader::get_app_data(0);

    let (user_space, entry, user_sp) = MemorySet::new_user_test(app);

    assert_eq!(entry, USER_TEXT_BASE);
    assert_eq!(user_sp, USER_STACK_TOP);

    let text_pte = user_space
        .translate(VirtAddr(USER_TEXT_BASE).floor())
        .expect("user text is not mapped");

    let stack_pte = user_space
        .translate(VirtAddr(USER_STACK_TOP - 1).floor())
        .expect("user stack is not mapped");

    #[cfg(target_arch = "riscv64")]
    {
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

    #[cfg(target_arch = "loongarch64")]
    {
        let kernel_va = crate::mm::kernel_phys_to_virt(crate::arch::MEMORY_START);
        assert!(
            user_space.translate(VirtAddr(kernel_va).floor()).is_none(),
            "LoongArch user page table should not contain kernel direct-map PTE in current design"
        );

        log::info!(
            "[mm] user MemorySet test passed: entry={:#x}, sp={:#x}, text_ppn={:?}, stack_ppn={:?}, kernel direct-map handled by TLB refill",
            entry,
            user_sp,
            text_pte.ppn(),
            stack_pte.ppn(),
        );
    }
}