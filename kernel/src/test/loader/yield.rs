//! loader.rs (RISC-V)

pub static USER_APP_0: &[u8] = &[
    // 先做 syscall yield
    0x01, 0x00, 0x00, 0x00, // yield
    // 然后 exit(0)
    0x00, 0x00, 0x00, 0x00,
];

pub static USER_APP_1: &[u8] = &[
    0x01, 0x00, 0x00, 0x00, // yield
    0x00, 0x01, 0x00, 0x00, // exit(1)
];


pub fn num_apps() -> usize {
    2
}

pub fn get_app_data(app_id: usize) -> &'static [u8] {
    match app_id {
        0 => USER_APP_0,
        1 => USER_APP_1,
        _ => panic!("bad app id {}", app_id),
    }
}