use core::fmt::{self, Write};

struct Console;

impl Write for Console {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        crate::io::uart::puts_raw(s);
        Ok(())
    }
}

pub fn _print(args: fmt::Arguments) {
    use core::fmt::Write;
    Console.write_fmt(args).unwrap();
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
        $crate::print!("\n")
    };
    ($($arg:tt)*) => {{
        $crate::io::console::_print(format_args!($($arg)*));
        $crate::io::console::_print(format_args!("\n"));
    }};
}