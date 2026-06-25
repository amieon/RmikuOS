#![no_std]
#![no_main]

use ulib::io::puts;
use ulib::process::exit;
use ulib::fs::{stat, Stat, STAT_TYPE_DIR};

#[no_mangle]
#[link_section = ".text.entry"]
pub extern "C" fn _start() -> ! {
    puts("hello from rust ulib\n");

    // 顺便 demo 一下调用更高层接口:stat 根目录
    let mut st = Stat::new();
    if stat(b"/", &mut st) >= 0 {
        if st.file_type == STAT_TYPE_DIR {
            puts("/ is a directory (stat works)\n");
        }
    }

    exit(0);
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    // panic 时退出码 1
    exit(1);
}
