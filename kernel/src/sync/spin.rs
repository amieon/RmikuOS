use core::cell::UnsafeCell;
use core::ops::{Deref, DerefMut};
use core::hint::spin_loop;
use core::sync::atomic::{AtomicBool, AtomicUsize, Ordering};

/// 带死锁检测的自旋锁
pub struct Mutex<T> {
    locked: AtomicBool,
    owner: AtomicUsize,
    line: AtomicUsize,
    data: UnsafeCell<T>,
}

unsafe impl<T: Send> Sync for Mutex<T> {}
unsafe impl<T: Send> Send for Mutex<T> {}

impl<T> Mutex<T> {
    pub const fn new(data: T) -> Self {
        Self {
            locked: AtomicBool::new(false),
            owner: AtomicUsize::new(!0),
            line: AtomicUsize::new(0),
            data: UnsafeCell::new(data),
        }
    }

    pub fn lock(&self) -> MutexGuard<'_, T> {
        self.lock_at(0)
    }

    pub fn lock_at(&self, line: u32) -> MutexGuard<'_, T> {
        let hart = crate::task::current_hart_id();

        while self
            .locked
            .compare_exchange(false, true, Ordering::Acquire, Ordering::Relaxed)
            .is_err()
        {
            if self.owner.load(Ordering::Acquire) == hart {
                let prev_line = self.line.load(Ordering::Relaxed);
                panic!(
                    "DEADLOCK: hart {} re-entering lock! previous lock at line {} (current line {})",
                    hart, prev_line, line
                );
            }
            spin_loop();
        }

        self.owner.store(hart, Ordering::Relaxed);
        self.line.store(line as usize, Ordering::Relaxed);
        MutexGuard { mutex: self }
    }

    pub fn try_lock(&self) -> Option<MutexGuard<'_, T>> {
        match self.locked.compare_exchange(
            false,
            true,
            Ordering::Acquire,
            Ordering::Relaxed,
        ) {
            Ok(_) => {
                self.owner.store(crate::task::current_hart_id(), Ordering::Relaxed);
                self.line.store(0, Ordering::Relaxed);
                Some(MutexGuard { mutex: self })
            }
            Err(_) => None,
        }
    }

    fn unlock(&self) {
        // === 关键修复：Release 确保 owner 写入在 locked 释放前可见 ===
        self.owner.store(!0, Ordering::Release);
        self.line.store(0, Ordering::Relaxed);
        self.locked.store(false, Ordering::Release);
    }
}

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

#[macro_export]
macro_rules! lock_detect {
    ($mutex:expr) => {
        $mutex.lock_at(line!())
    };
}