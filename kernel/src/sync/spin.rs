use core::cell::UnsafeCell;
use core::ops::{Deref, DerefMut};
use core::hint::spin_loop;
use core::sync::atomic::{AtomicBool, AtomicUsize, Ordering};

/// 带死锁检测的自旋锁
pub struct Mutex<T> {
    locked: AtomicBool,
    owner: AtomicUsize,      // !0 = 无持有者
    line: AtomicUsize,       // 持有者是在哪一行代码拿的锁
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

    /// 普通 lock（不带行号，死锁只报 hart id）
    pub fn lock(&self) -> MutexGuard<'_, T> {
        self.lock_at(0)
    }

    /// 带代码行号的 lock，死锁时精确定位
    pub fn lock_at(&self, line: u32) -> MutexGuard<'_, T> {
        let hart =crate::task::current_hart_id();  // ← 确保这个函数可用

        while self
            .locked
            .compare_exchange(false, true, Ordering::Acquire, Ordering::Relaxed)
            .is_err()
        {
            // === 死锁检测：同核重入 ===
            if self.owner.load(Ordering::Relaxed) == hart {
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
        self.owner.store(!0, Ordering::Relaxed);
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

/// 宏：自动传入当前代码行号
#[macro_export]
macro_rules! lock_detect {
    ($mutex:expr) => {
        $mutex.lock_at(line!())
    };
}