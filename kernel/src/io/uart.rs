use crate::arch;

pub struct Uart {
    base: usize,
}

impl Uart {
    pub const fn new(base: usize) -> Self {
        Self { base }
    }

    pub fn init(&self) {}

    fn putc(&self, c: u8) {
        let p = self.base as *mut u8;
        unsafe {
            while p.add(5).read_volatile() & 0x20 == 0 {}
            p.write_volatile(c);
        }
    }

    pub fn puts(&self, s: &str) {
        for c in s.bytes() {
            if c == b'\n' {
                self.putc(b'\r');
            }
            self.putc(c);
        }
    }
}

static mut UART_DEV: Uart = Uart::new(arch::UART_BASE);

pub fn init() {
    unsafe {
        UART_DEV.init();
    }
}

pub fn puts_raw(s: &str) {
    unsafe {
        UART_DEV.puts(s);
    }
}