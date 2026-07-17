use core::fmt::{self, Write};

use crate::sync::sync::SpinLock;

static CONSOLE_LOCK: SpinLock = SpinLock::new();

struct Console;

impl Write for Console {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        crate::io::uart::puts_raw(s);
        Ok(())
    }
}

pub fn _print(args: fmt::Arguments) {
    CONSOLE_LOCK.lock();

    let mut console = Console;
    let _ = console.write_fmt(args);

    CONSOLE_LOCK.unlock();
}

#[macro_export]
macro_rules! print {
    ($($arg:tt)*) => {
        $crate::io::console::_print(format_args!($($arg)*))
    };
}

#[macro_export]
macro_rules! println {
    () => {
        $crate::io::console::_print(format_args!("\n"))
    };
    ($($arg:tt)*) => {{
        $crate::io::console::_print(format_args!($($arg)*));
        $crate::io::console::_print(format_args!("\n")) 
    }};
}

pub fn _trap_log(args: fmt::Arguments<'_>) {
    CONSOLE_LOCK.lock();

    let mut console = Console;
    let _ = write!(console, "[CPU{}] ", crate::arch::hartid());
    let _ = console.write_fmt(args);
    let _ = console.write_str("\n");

    CONSOLE_LOCK.unlock();
}