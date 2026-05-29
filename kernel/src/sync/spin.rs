use core::cell::UnsafeCell;
use core::ops::{Deref, DerefMut};
use core::hint::spin_loop;

/// 简单的自旋锁
pub struct Mutex<T> {
    locked: core::sync::atomic::AtomicBool,
    data: UnsafeCell<T>,
}

// 必须保证 T 是 Send
unsafe impl<T: Send> Sync for Mutex<T> {}
unsafe impl<T: Send> Send for Mutex<T> {}

impl<T> Mutex<T> {
    /// 创建一个新的 Mutex
    pub const fn new(data: T) -> Self {
        Self {
            locked: core::sync::atomic::AtomicBool::new(false),
            data: UnsafeCell::new(data),
        }
    }

    /// 获取锁
    pub fn lock(&self) -> MutexGuard<'_, T> {
        while self
            .locked
            .compare_exchange(false, true,
                core::sync::atomic::Ordering::Acquire,
                core::sync::atomic::Ordering::Relaxed)
            .is_err()
        {
            spin_loop();
        }
        MutexGuard { mutex: self }
    }

    /// 解锁（由 MutexGuard 自动调用）
    fn unlock(&self) {
        self.locked.store(false, core::sync::atomic::Ordering::Release);
    }
}

/// RAII 锁守护
pub struct MutexGuard<'a, T> {
    mutex: &'a Mutex<T>,
}

impl<'a, T> Deref for MutexGuard<'a, T> {
    type Target = T;
    fn deref(&self) -> &T {
        unsafe { &*self.mutex.data.get() }
    }
}

impl<'a, T> DerefMut for MutexGuard<'a, T> {
    fn deref_mut(&mut self) -> &mut T {
        unsafe { &mut *self.mutex.data.get() }
    }
}

impl<'a, T> Drop for MutexGuard<'a, T> {
    fn drop(&mut self) {
        self.mutex.unlock();
    }
}