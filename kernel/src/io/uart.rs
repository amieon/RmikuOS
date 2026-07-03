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

use core::ptr::{read_volatile, write_volatile};

const UART_THR: usize = 0; // Transmit Holding Register
const UART_LSR: usize = 5; // Line Status Register
const UART_LSR_THRE: u8 = 1 << 5; // Transmit Holding Register Empty

#[inline]
fn uart_base() -> *mut u8 {
    crate::arch::UART_BASE as *mut u8
}

pub fn putchar_raw(ch: u8) {
    let uart = uart_base();

    unsafe {
        while read_volatile(uart.add(UART_LSR)) & UART_LSR_THRE == 0 {}
        write_volatile(uart.add(UART_THR), ch);
    }
}


pub fn print_i32(num: i32) {

    let mut buffer = [0u8; 16];
    let mut idx = buffer.len();


    let mut n = num as i64;
    let is_negative = n < 0;
    if is_negative {
        n = -n;
    }


    if n == 0 {
        puts_raw("0");
        return;
    }


    while n > 0 {
        idx -= 1;
        buffer[idx] = (n % 10) as u8 + b'0';
        n /= 10;
    }


    if is_negative {
        idx -= 1;
        buffer[idx] = b'-';
    }


    let s = core::str::from_utf8(&buffer[idx..]).unwrap();
    puts_raw(s);
}

#[cfg(target_arch = "loongarch64")]
pub fn putchar_phys_raw(ch: u8) {
    let uart = crate::arch::UART_PADDR as *mut u8;

    unsafe {
        while read_volatile(uart.add(UART_LSR)) & UART_LSR_THRE == 0 {}
        write_volatile(uart.add(UART_THR), ch);
    }
}


pub fn getchar_raw() -> u8 {
    let uart = crate::mm::kernel_phys_to_virt(crate::arch::UART_PADDR) as *mut u8;

    unsafe {

        while core::ptr::read_volatile(uart.add(5)) & 0x01 == 0 {
            core::hint::spin_loop();
        }

        core::ptr::read_volatile(uart.add(0))
    }
}


pub fn try_getchar_raw() -> Option<u8> {
    let uart = crate::mm::kernel_phys_to_virt(crate::arch::UART_PADDR) as *mut u8;
    unsafe {
        if core::ptr::read_volatile(uart.add(5)) & 0x01 != 0 {
            Some(core::ptr::read_volatile(uart.add(0)))
        } else {
            None
        }
    }
}