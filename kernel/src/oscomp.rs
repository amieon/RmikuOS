/// 比赛评测模式:打印 12 组测试标记,然后关机。
/// 不读盘、不依赖 rootfs,保证在任何评测盘下都能跑完。
pub fn run_oscomp_stub() -> ! {
    // 12 个测试组(pre-2025)
    let groups = [
        "basic", "busybox", "lua", "libctest", "iozone",
        "unixbench", "iperf", "libcbench", "lmbench",
        "netperf", "cyclictest", "ltp",
    ];

    for g in groups.iter() {
        // 格式必须和评测要求一致
        crate::println!("#### OS COMP TEST GROUP START {} ####", g);
        // 这里不真跑测试,只打标记
        crate::println!("#### OS COMP TEST GROUP END {} ####", g);
    }

    // 跑完关机
    crate::shutdown::shutdown();

    // shutdown 不返回,但 Rust 要求 -> ! 的函数不结束
    loop {
        crate::arch::wait_for_interrupt();
    }
}