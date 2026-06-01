use alloc::sync::Arc;

pub type FileRef = Arc<dyn File>;

pub trait File: Send + Sync {
    fn readable(&self) -> bool;
    fn writable(&self) -> bool;

    fn is_dir(&self) -> bool {
        false
    }

    fn read(&self, buf: &mut [u8]) -> isize;
    fn write(&self, buf: &[u8]) -> isize;

    fn getdents(&self, _buf: &mut [u8]) -> isize {
        -1
    }
}