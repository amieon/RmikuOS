// src/main.rs
#![no_std]
#![no_main]

mod arch;
mod sync;
mod uart;
mod io;


use core::sync::atomic::{AtomicBool, Ordering};
use crate::print;
use crate::println;
use io::panic_handler;

static MASTER_READY: AtomicBool = AtomicBool::new(false);

struct HartLocal {
    /// 我是几号核
    id: usize,
    /// 我执行了多少轮工作循环
    tick: usize,
    /// 我是否已经初始化完毕
    ready: bool,
}

impl HartLocal {
    const fn new() -> Self {
        Self {
            id: 0,
            tick: 0,
            ready: false,
        }
    }
}

// 工牌墙，一共 MAX_HARTS 个格子
static mut HART_LOCALS: [HartLocal; arch::MAX_HARTS] = [
    HartLocal::new(),
    HartLocal::new(),
    HartLocal::new(),
    HartLocal::new(),
    HartLocal::new(),
    HartLocal::new(),
    HartLocal::new(),
    HartLocal::new(),
];



#[no_mangle]
pub extern "C" fn rust_main(id: usize) -> ! {
    {
        let local = unsafe { &mut HART_LOCALS[id] };
        local.id = id;
    }


    if id == 0 {
        // 主核路径 
        primary_init();

        // 点亮信号灯：从核们，可以进来了！
        // Release 保证上面的所有初始化对从核可见
        MASTER_READY.store(true, Ordering::Release);

        println!("主核初始化完成，从核可以进入了。");
    } else {
        // 从核路径 
        // 自旋等待主核就绪
        // Acquire 保证看到 true 时，主核的初始化也都可见
        while !MASTER_READY.load(Ordering::Acquire) {
            core::hint::spin_loop();
        }

        secondary_init(id);
    }


    kernel_loop(id);
}


fn primary_init() {
    // 始化 UART，之后所有核都能用 println!
    uart::init();

    println!("==== ChCore-Rust 多核启动 ====");
    println!("架构: {}", arch::NAME);
    println!("最大支持核数: {}", arch::MAX_HARTS);
    println!("==============================");

    // 未来在这里初始化：
    //    - 内存分配器
    //    - 中断控制器（PLIC / 7A2000 中断控制器）
    //    - 定时器
    //    - 进程调度器
    //    ...

    println!("主核初始化完毕。");
}


fn secondary_init(id: usize) {
    // 标记自己就绪
    unsafe {
        HART_LOCALS[id].ready = true;
    }
    println!("从核 {} 就绪！", id);
}


fn kernel_loop(id: usize) -> ! {
    println!("核 {} 进入工作循环。", id);

    loop {
        let local = unsafe { &mut HART_LOCALS[id] };
        local.tick += 1;

        // 每个核的工作，模拟做一些事情
        do_work(id, local.tick);

        // 主动让出 CPU 未来改成调度器，现在用忙等模拟
        for _ in 0..1_000_000 {
            core::hint::spin_loop();
        }
    }
}

/// 模拟每个核的工作
fn do_work(id: usize, tick: usize) {
    if tick % 100 == 0 {
        println!("核 {} 工作中... (第 {} 轮)", id, tick);
    }

    match id {
        0 => {
            // 主核检查其他核的状态
            if tick % 500 == 0 {
                print_heartbeat();
            }
        }
        1 => {
            
        }
        2 => {
            
        }
        _ => {}
    }
}

/// 打印所有核的心跳状态
fn print_heartbeat() {
    println!("──── 心跳检查 ────");
    for i in 0..arch::MAX_HARTS {
        let local = unsafe { &HART_LOCALS[i] };
        if local.ready {
            println!("  核 {}: 存活 (tick={})", i, local.tick);
        }
    }
    println!("──────────────────");
}



