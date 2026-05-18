// src/uart.rs
use crate::arch;
use crate::sync::SpinLock;
use core::fmt;

pub struct Uart {
    base: usize,
}

impl Uart {
    pub const fn new(base: usize) -> Self {
        Self { base }
    }

    pub fn init(&self) {
        // QEMU 的 NS16550A 通常不需要额外初始化
    }

    fn putc(&self, c: u8) {
        let p = self.base as *mut u8;
        unsafe {
            // 等待发送保持寄存器为空
            while p.add(5).read_volatile() & 0x20 == 0 {}
            p.write_volatile(c);
        }
    }
}

impl fmt::Write for Uart {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        for c in s.bytes() {
            if c == b'\n' {
                self.putc(b'\r'); // 回车换行
            }
            self.putc(c);
        }
        Ok(())
    }
}


// 全局 UART（加自旋锁，多核安全）

static UART: SpinLock = SpinLock::new();
static mut UART_DEV: Uart = Uart::new(arch::UART_BASE);

/// 获取 UART 的独占访问权
pub fn get_uart() -> impl fmt::Write + 'static {
    // 这里简化了，真正的 SpinLockGuard 需要持有锁引用
    // 我们用更直接的方式
    UartWriter
}

struct UartWriter;

impl fmt::Write for UartWriter {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        UART.lock();
        let uart = unsafe { &mut UART_DEV };
        for c in s.bytes() {
            if c == b'\n' {
                uart.putc(b'\r');
            }
            uart.putc(c);
        }
        UART.unlock();
        Ok(())
    }
}

/// 初始化（只需要主核调用一次）
pub fn init() {
    unsafe { UART_DEV.init(); }
}


// 低级别输出（panic 时使用，不加锁，最后的手段）
pub fn puts_raw(s: &str) {
    let uart = unsafe { &UART_DEV };
    for c in s.bytes() {
        uart.putc(c);
    }
}