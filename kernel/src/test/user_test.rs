pub fn user_test() -> ! {
    let app: &[u8] = &[
        0x73, 0x00, 0x00, 0x00, // ecall
    ];

    let (user_space, entry, user_sp) = crate::mm::MemorySet::new_user_test(app);
    let trap_cx = crate::trap::TrapContext::app_init_context(entry, user_sp);

    crate::task::run_user(user_space, trap_cx);
}