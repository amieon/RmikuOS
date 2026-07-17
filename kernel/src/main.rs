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
mod drivers;
mod pci;
mod math;
mod oscomp;

#[macro_use]
mod io;

use core::sync::atomic::{AtomicBool, AtomicUsize, Ordering};

use crate::sync::*;

static MASTER_READY: AtomicBool = AtomicBool::new(false);
static PRIMARY_HART: AtomicUsize = AtomicUsize::new(!0);

pub struct HartLocal {
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

pub static HART_LOCALS: [HartLocal; arch::MAX_HARTS] = [
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

#[cfg(target_arch = "riscv64")]
unsafe extern "C" {
    static boot_entry_addr: usize;
}

#[cfg(target_arch = "riscv64")]
fn sbi_hart_start(hartid: usize, start_addr: usize) -> bool {
    let mut error: isize;

    unsafe {
        core::arch::asm!(
            "ecall",
            inlateout("a0") hartid => error,
            inlateout("a1") start_addr => _,
            in("a2") 0usize,
            in("a7") 0x48534Dusize,
            in("a6") 0usize,
            options(nostack),
        );
    }

    error == 0
}

#[no_mangle]
pub extern "C" fn rust_main(id: usize) -> ! {
    if id >= arch::MAX_HARTS {
        park_forever();
    }

    HART_LOCALS[id].id.store(id, Ordering::Relaxed);

    #[cfg(target_arch = "loongarch64")]
    let is_primary = {
        if id == 0 {
            PRIMARY_HART.store(0, Ordering::SeqCst);
            true
        } else {
            false
        }
    };

    #[cfg(target_arch = "riscv64")]
    let is_primary = PRIMARY_HART
        .compare_exchange(
            !0,
            id,
            Ordering::SeqCst,
            Ordering::Relaxed,
        )
        .is_ok();

    if is_primary {
        primary_init(id);
    } else {
        while !MASTER_READY.load(Ordering::Acquire) {
            core::hint::spin_loop();
        }

        secondary_init(id);
    }
}

fn primary_init(id: usize) -> ! {
    io::uart::init();
    io::logger::init();

    trap::init();

    mm::init();
    mm::init_paging();

    drivers::block::ext4_image::test_ext4_magic();

    let (ext4_dev, fat_dev) = drivers::block::discover_disks::discover_disks();

    #[cfg(feature = "oscomp")]
    {
        crate::oscomp::run_oscomp_stub();
    }

    let rootfs_device = ext4_dev.unwrap_or_else(|| {
        log::warn!("[disk] no ext4 disk, fallback to ramdisk");
        crate::drivers::block::ext4_image::rootfs_ramdisk()
    });

    fs::ext4fs::init(rootfs_device);
    fs::tmpfs::init();

    if let Some(fdev) = fat_dev {
        fs::fatfs::init(fdev);
    } else {
        log::warn!("[disk] no FAT disk found, /fat not mounted");
    }


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
    
    timer::init();

    drivers::net::init();

    HART_LOCALS[id].ready.store(true, Ordering::Release);
    MASTER_READY.store(true, Ordering::Release);

    println!("主核初始化完成，从核可以进入了。");

    task::run_tasks();
}

fn secondary_init(id: usize) -> ! {
    crate::mm::activate_kernel_page_table();
    crate::arch::flush_tlb();

    trap::init();
    timer::init();

    HART_LOCALS[id].ready.store(true, Ordering::Release);

    println!("从核 {} 就绪！", id);

    task::run_tasks();
}

fn park_forever() -> ! {
    loop {
        core::hint::spin_loop();
    }
}