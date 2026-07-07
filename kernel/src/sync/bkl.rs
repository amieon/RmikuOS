// kernel/src/sync/bkl.rs
use core::sync::atomic::{AtomicBool, Ordering};

pub struct SpinLock {
    locked: AtomicBool,
}

impl SpinLock {
    pub const fn new() -> Self {
        Self { locked: AtomicBool::new(false) }
    }
    pub fn lock(&self) {
        while self.locked.compare_exchange(false, true, Ordering::Acquire, Ordering::Relaxed).is_err() {
            core::hint::spin_loop();
        }
    }
    pub fn unlock(&self) {
        self.locked.store(false, Ordering::Release);
    }
}

static BKL: SpinLock = SpinLock::new();

pub fn bkl_enter() { BKL.lock(); }
pub fn bkl_exit()  { BKL.unlock(); }
