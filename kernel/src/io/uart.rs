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