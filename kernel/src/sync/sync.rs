// src/sync.rs
// 最简单有效的自旋锁，保护 UART 等共享资源

use core::sync::atomic::{AtomicBool, Ordering};

use crate::arch::{intr_disable, intr_get, intr_enable};

use core::cell::UnsafeCell;


pub struct SpinLock {
    locked: AtomicBool,
    // 保存 lock 前的中断状态，以便 unlock 时正确恢复
    // UnsafeCell 因为我们在 lock/unlock 配对中单线程访问
    saved_intr: UnsafeCell<bool>,
}

// SpinLock 在多核间共享是安全的
unsafe impl Sync for SpinLock {}

impl SpinLock {
    pub const fn new() -> Self {
        Self {
            locked: AtomicBool::new(false),
            saved_intr: UnsafeCell::new(false),
        }
    }

    pub fn lock(&self) {
        loop {
            let intr_was_on = intr_get();
            intr_disable();

            if self
                .locked
                .compare_exchange(false, true, Ordering::Acquire, Ordering::Relaxed)
                .is_ok()
            {
                // 保存中断状态，供 unlock 恢复
                unsafe { *self.saved_intr.get() = intr_was_on; }
                return;
            }

            if intr_was_on {
                intr_enable();
            }
            core::hint::spin_loop();
        }
    }

    pub fn unlock(&self) {
        let intr_was_on = unsafe { *self.saved_intr.get() };
        self.locked.store(false, Ordering::Release);
        if intr_was_on {
            intr_enable();
        }
        // 如果 lock 前中断就是关的，unlock 后也保持关
    }
}
// 安全包装：加锁后返回一个守卫，离开作用域自动释放
pub struct SpinLockGuard<'a, T> {
    lock: &'a SpinLock,
    data: &'a mut T,
}

impl<'a, T> Drop for SpinLockGuard<'a, T> {
    fn drop(&mut self) {
        self.lock.unlock();
    }
}

impl<'a, T> core::ops::Deref for SpinLockGuard<'a, T> {
    type Target = T;
    fn deref(&self) -> &T {
        self.data
    }
}

impl<'a, T> core::ops::DerefMut for SpinLockGuard<'a, T> {
    fn deref_mut(&mut self) -> &mut T {
        self.data
    }
}