// src/main.rs
#[no_mangle]
pub extern "C" fn rust_main(hartid: usize) -> ! {
    // 主核（hart 0）负责初始化
    if hartid == 0 {
        init_platform();
        // 启动其他核
        for id in 1..MAX_HARTS {
            arch::start_secondary_hart(id);
        }
    }
    
    // 所有核都进入这里
    kernel_main(hartid);
}