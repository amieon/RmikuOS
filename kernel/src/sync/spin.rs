use core::cell::UnsafeCell;
use core::ops::{Deref, DerefMut};
use core::hint::spin_loop;
use core::sync::atomic::{AtomicBool, AtomicUsize, Ordering};

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

        if self.owner.load(Ordering::Relaxed) == hart {
            let prev_line = self.line.load(Ordering::Relaxed);
            panic!(
                "DEADLOCK: hart {} re-entering lock {:p}! prev line {} (current line {})",
                hart, self as *const Self, prev_line, line
            );
        }
        // ========== 死锁检测：CAS 之前 ==========
        // 只有本核能把 owner 设成自己的 hart id，
        // 所以 Relaxed 足够——本核一定看到自己之前的值。
        if self.owner.load(Ordering::Relaxed) == hart {
            let prev_line = self.line.load(Ordering::Relaxed);
            panic!(
                "DEADLOCK: hart {} re-entering lock! previous lock at line {} (current line {})",
                hart, prev_line, line
            );
        }

        // 自旋抢锁
        while self
            .locked
            .compare_exchange(false, true, Ordering::Acquire, Ordering::Relaxed)
            .is_err()
        {
            while self.locked.load(Ordering::Acquire) {
                spin_loop();
            }
        }

        // 抢到了，标 owner
        self.owner.store(hart, Ordering::Release);
        self.line.store(line as usize, Ordering::Release);
        crate::task::preempt_disable();
        MutexGuard { mutex: self }
    }

    pub fn try_lock(&self) -> Option<MutexGuard<'_, T>> {
        let hart = crate::task::current_hart_id();
        if self.owner.load(Ordering::Relaxed) == hart {
            // 之前死锁检测保留
        }
        if self.locked.compare_exchange(false, true, Ordering::Acquire, Ordering::Relaxed).is_ok() {
            self.owner.store(hart, Ordering::Release);
            self.line.store(0, Ordering::Release);
            // 临时调试：打印谁拿走了 try_lock
            //crate::println!("[try_lock] hart {} got TASK_MANAGER, caller line not available", hart);
            crate::task::preempt_disable();
            Some(MutexGuard { mutex: self })
        } else {
            None
        }
    }

    fn unlock(&self) {
        crate::task::preempt_enable();
        self.owner.store(!0, Ordering::Release);
        self.line.store(0, Ordering::Release);
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