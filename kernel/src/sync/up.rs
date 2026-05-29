use core::cell::UnsafeCell;

pub struct UPSafeCell<T> {
    inner: UnsafeCell<T>,
}

unsafe impl<T> Sync for UPSafeCell<T> {}

impl<T> UPSafeCell<T> {
    pub const fn new(value: T) -> Self {
        Self {
            inner: UnsafeCell::new(value),
        }
    }

    pub fn exclusive_access(&self) -> &mut T {
        unsafe { &mut *self.inner.get() }
    }
}