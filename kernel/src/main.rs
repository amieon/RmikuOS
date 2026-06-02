// kernel/src/main.rs
#![no_std]
#![no_main]

extern crate alloc;

mod arch;
mod sync;
mod trap;
mod timer;
mod mm;
mod panic;
mod test;
mod task;
mod syscall;
mod loader;
mod fs;
mod block;

#[macro_use]
mod io;


use core::sync::atomic::{AtomicBool, AtomicUsize, Ordering};

use crate::sync::*;




static MASTER_READY: AtomicBool = AtomicBool::new(false);

struct HartLocal {
    /// 我是几号核
    id: AtomicUsize,
    /// 我执行了多少轮工作循环
    tick: AtomicUsize,
    /// 我是否已经初始化完毕
    ready: AtomicBool,
}

impl HartLocal {
    const fn new() -> Self {
        Self {
            id: AtomicUsize::new(0),
            tick: AtomicUsize::new(0),
            ready: AtomicBool::new(false),
        }
    }
}

// 工牌墙，一共 MAX_HARTS 个格子。
// 注意：这里用原子字段，避免多核同时读写 tick/ready 时形成 Rust 数据竞争。
static HART_LOCALS: [HartLocal; arch::MAX_HARTS] = [
    HartLocal::new(),
    HartLocal::new(),
    HartLocal::new(),
    HartLocal::new(),
    HartLocal::new(),
    HartLocal::new(),
    HartLocal::new(),
    HartLocal::new(),
];

unsafe extern "C" {
    static _kernel_start: u8;
    static _kernel_end: u8;
    static _stext: u8;
    static _etext: u8;
    static _srodata: u8;
    static _erodata: u8;
    static _sdata: u8;
    static _edata: u8;
    static _sbss: u8;
    static _ebss: u8;
}




#[no_mangle]
pub extern "C" fn rust_main(id: usize) -> ! {
    if id >= arch::MAX_HARTS {
        park_forever();
    }
    
    HART_LOCALS[id].id.store(id, Ordering::Relaxed);

    if id == 0 {
        log::info!("rust_main at high half");
        log::info!("kernel va: {:#x}..{:#x}", { core::ptr::addr_of!(_kernel_start) as usize } as usize, { core::ptr::addr_of!(_kernel_end) as usize } as usize);
        // 主核路径
        primary_init();

        // 点亮信号灯：从核们，可以进来了！
        // Release 保证上面的所有初始化对从核可见。
        MASTER_READY.store(true, Ordering::Release);
        println!("主核初始化完成，从核可以进入了。");
    } else {
        // 从核路径：Acquire 保证看到 true 时，主核初始化也都可见。
        while !MASTER_READY.load(Ordering::Acquire) {
            core::hint::spin_loop();
        }
        secondary_init(id);
    }

    kernel_loop(id);
}

fn primary_init() {
    // 初始化 UART，之后所有核都能用 println!。
    io::uart::init();
    io::logger::init();
    trap::init();

    mm::init();
    test::heap_test::heap_test();
    test::frame_alloc_test::frame_alloc_test();
    test::page_table_test::page_table_test();

    test::memory_set_test::memory_set_test();

    mm::init_paging();

    test::user_memory_set_test::user_memory_set_test();

    timer::init();

    test::block_cache_tset::test_block_cache();
    test::block_test::test_ramdisk();
    block::ext4_image::test_ext4_magic();


    let rootfs = crate::block::ext4_image::rootfs_ramdisk();
    crate::fs::ext4fs::init(rootfs);

    HART_LOCALS[0].ready.store(true, Ordering::Release);

    log::info!("logger initialized");
    log::info!("==== RmikuOS 多核启动 ====");
    log::info!("架构: {}", arch::NAME);
    log::info!("最大支持核数: {}", arch::MAX_HARTS);


    // 未来在这里初始化：
    // - 内存分配器
    // - 中断控制器（PLIC / 7A2000 中断控制器）
    // - 定时器
    // - 进程调度器
    // ...
    println!("主核初始化完毕。");

    task::init();
    task::run_first_task();
    
}

fn secondary_init(id: usize) {
    HART_LOCALS[id].ready.store(true, Ordering::Release);
    println!("从核 {} 就绪！", id);
}

fn kernel_loop(id: usize) -> ! {
    println!("核 {} 进入工作循环。", id);

    loop {
        let tick = HART_LOCALS[id]
            .tick
            .fetch_add(1, Ordering::Relaxed)
            .wrapping_add(1);

        // 每个核的工作，模拟做一些事情。
        do_work(id, tick);

        // 主动让出 CPU：未来改成调度器，现在用忙等模拟。
        for _ in 0..1_000_000 {
            core::hint::spin_loop();
        }
    }
}

/// 模拟每个核的工作。
fn do_work(id: usize, tick: usize) {
    if tick % 100 == 0 {
        println!("核 {} 工作中... (第 {} 轮)", id, tick);
    }

    match id {
        0 => {
            // 主核检查其他核的状态。
            if tick % 500 == 0 {
                print_heartbeat();
            }
        }
        1 => {}
        2 => {}
        _ => {}
    }
}

/// 打印所有核的心跳状态。
fn print_heartbeat() {
    println!("──── 心跳检查 ────");

    for i in 0..arch::MAX_HARTS {
        let local = &HART_LOCALS[i];
        if local.ready.load(Ordering::Acquire) {
            let id = local.id.load(Ordering::Relaxed);
            let tick = local.tick.load(Ordering::Relaxed);
            println!(" 核 {}: 存活 (tick={})", id, tick);
        }
    }

    println!("──────────────────");
}

fn park_forever() -> ! {
    loop {
        core::hint::spin_loop();
    }
}
