use alloc::sync::Arc;

use crate::fs::stat::Stat;

pub type FileRef = Arc<dyn File>;

pub enum PipeCloseKind {
    Nothing,
    ReaderGone,
    WriterGone,
}

pub trait File: Send + Sync {
    fn readable(&self) -> bool;
    fn writable(&self) -> bool;

    fn is_dir(&self) -> bool {
        false
    }

    fn stat(&self) -> Stat;

    fn read(&self, buf: &mut [u8]) -> isize;
    fn read_nonblock(&self, buf: &mut [u8]) -> isize {
        self.read(buf)
    }

    fn write(&self, buf: &[u8]) -> isize;

    fn getdents(&self, _buf: &mut [u8]) -> isize {
        -1
    }

    fn on_fork(&self) {}
    fn on_close_kind(&self) -> PipeCloseKind {PipeCloseKind::Nothing}
}