use core::fmt::Write;
use core::panic::PanicInfo;


macro_rules! print {
    ($($arg:tt)*) => {{
        let _ = core::fmt::write(&mut uart::get_uart(), format_args!($($arg)*));
    }};
}

macro_rules! println {
    () => { print!("\n"); };
    ($($arg:tt)*) => {{
        print!("[CPU{}] ", arch::hartid());
        print!($($arg)*);
        print!("\n");
    }};
}

#[panic_handler]
pub fn panic_handler(info: &PanicInfo) -> ! {
    let id = arch::hartid();


    uart::puts_raw("\nKERNEL PANIC\n");
    uart::puts_raw("CPU: ");
    uart::puts_raw(&alloc::format!("{}", id)); 
    uart::puts_raw("\n");

    if let Some(loc) = info.location() {
        uart::puts_raw("At: ");
        uart::puts_raw(loc.file());
        uart::puts_raw(":");
        uart::puts_raw("\n");
    }

    loop {
        core::hint::spin_loop();
    }
}