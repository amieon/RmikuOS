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
mod fs;
mod block;
mod pci;
mod math;
mod shutdown;
mod oscomp;

#[macro_use]
mod io;

use core::sync::atomic::{AtomicBool, AtomicUsize, Ordering};

use crate::{io::uart::puts_raw, sync::*, task::run_tasks};

static MASTER_READY: AtomicBool = AtomicBool::new(false);

struct HartLocal {
    id: AtomicUsize,
    tick: AtomicUsize,
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

static HART_LOCALS: [HartLocal; arch::MAX_HARTS] = [
    HartLocal::new(), HartLocal::new(), HartLocal::new(), HartLocal::new(),
    HartLocal::new(), HartLocal::new(), HartLocal::new(), HartLocal::new(),
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

// === RISC-V: boot.S 里定义的全局变量，存储 _start 地址 ===
#[cfg(target_arch = "riscv64")]
extern "C" {
    static boot_entry_addr: usize;
}

// === RISC-V: SBI HSM 唤醒其他 hart ===
#[cfg(target_arch = "riscv64")]
fn sbi_hart_start(hartid: usize, start_addr: usize) -> bool {
    let mut error: isize;
    unsafe {
        core::arch::asm!(
            "ecall",
            inlateout("a0") hartid => error,
            inlateout("a1") start_addr => _,
            in("a2") 0,
            in("a7") 0x48534D,
            in("a6") 0,
        );
    }
    error == 0
}


static PRIMARY_HART: AtomicUsize = AtomicUsize::new(!0); // !0 = 未初始化

#[no_mangle]
pub extern "C" fn rust_main(id: usize) -> ! {

    if id >= arch::MAX_HARTS {
        park_forever();
    }

    HART_LOCALS[id].id.store(id, Ordering::Relaxed);

    // 第一个到达的核成为主核（无论 id 是多少）
    let is_primary = PRIMARY_HART.compare_exchange(
        !0,
        id,
        Ordering::SeqCst,
        Ordering::Relaxed,
    ).is_ok();

    if is_primary {
        primary_init(id);
        task::run_first_task();
    } else {
        while !MASTER_READY.load(Ordering::Acquire) {
            core::hint::spin_loop();
        }
        secondary_init(id);
        run_tasks();
    }
}

fn primary_init(id: usize) {

    io::uart::init();
    io::logger::init();
    trap::init();

    mm::init();
    mm::init_paging();

    block::ext4_image::test_ext4_magic();

    let (ext4_dev, fat_dev) = block::discover_disks::discover_disks();

    timer::init();

    #[cfg(feature = "oscomp")]
    {
        crate::oscomp::run_oscomp_stub();
    }

    let rootfs_device = ext4_dev.unwrap_or_else(|| {
        log::warn!("[disk] no ext4 disk, fallback to ramdisk");
        crate::block::ext4_image::rootfs_ramdisk()
    });
    fs::ext4fs::init(rootfs_device);
    fs::tmpfs::init();

    if let Some(fdev) = fat_dev {
        fs::fatfs::init(fdev);
    } else {
        log::warn!("[disk] no FAT disk found, /fat not mounted");
    }

    HART_LOCALS[id].ready.store(true, Ordering::Release);

    log::info!("logger initialized");
    log::info!("==== RmikuOS 多核启动 ====");
    log::info!("架构: {}", arch::NAME);
    log::info!("最大支持核数: {}", arch::MAX_HARTS);

    pub const BOOT_BANNER: &str = r#"
     ____            _ _         ___  ____  
    |  _ \ _ __ ___ (_) | ___   / _ \/ ___| 
    | |_) | '_ ` _ \| | |/ / | | | | \___ \ 
    |  _ <| | | | | | |   <| |_| |_| |___) |
    |_| \_\_| |_| |_|_|_|\_\\___/___/|____/ 

        RmikuOS - Rusty tiny OS kernel
    "#;
    println!("主核初始化完毕。");
    println!("{}", BOOT_BANNER);

    task::init();
        // === RISC-V: 唤醒其他 hart ===
    #[cfg(target_arch = "riscv64")]
    {
        let entry = unsafe { boot_entry_addr };
        for i in 0..arch::MAX_HARTS {
            if i == id {
                continue;
            }
            let ok = sbi_hart_start(i, entry);
            if !ok {
                log::warn!("[smp] failed to start hart {}", i);
            }
        }
    }


    MASTER_READY.store(true, Ordering::Release);
    println!("主核初始化完成，从核可以进入了。");
    run_tasks();
}

fn secondary_init(id: usize) {
    crate::mm::activate_kernel_page_table();
    crate::arch::flush_tlb();
    trap::init();
    //timer::init();
    

    HART_LOCALS[id].ready.store(true, Ordering::Release);
    println!("从核 {} 就绪！", id);
    run_tasks();
}

fn kernel_loop(id: usize) -> ! {
    println!("核 {} 进入工作循环。", id);

    loop {
        let tick = HART_LOCALS[id]
            .tick
            .fetch_add(1, Ordering::Relaxed)
            .wrapping_add(1);

        do_work(id, tick);

        for _ in 0..1_000_000 {
            core::hint::spin_loop();
        }
    }
}

fn do_work(id: usize, tick: usize) {
    if tick % 100 == 0 {
        //println!("核 {} 工作中... (第 {} 轮)", id, tick);
    }

    match id {
        0 => {
            if tick % 500 == 0 {
                print_heartbeat();
            }
        }
        1 => {}
        2 => {}
        _ => {}
    }
}

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