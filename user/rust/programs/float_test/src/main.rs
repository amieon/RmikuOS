#![no_std]
#![no_main]
use ulib::io::puts;
use ulib::process::{exit, fork};

#[no_mangle]
#[link_section = ".text.entry"]
pub extern "C" fn _start() -> ! {
    let pid = fork();
    
    if pid == 0 {
        // 子进程:累加 0.5(能精确表示),100万次 = 500000
        let mut s = 0.0f32;
        for _ in 0..1_000_000 {
            s += 0.5f32;
        }
        puts("child sum = ");
        ulib::io::put_int(s as u64);   // 期望 500000
        puts("\n");
        exit(0);
    } else {
        // 父进程:累加 0.25(能精确表示),100万次 = 250000
        let mut s = 0.0f32;
        for _ in 0..1_000_000 {
            s += 0.25f32;
        }
        puts("parent sum = ");
        ulib::io::put_int(s as u64);   // 期望 250000
        puts("\n");
        exit(0);
    }
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    exit(1);
}