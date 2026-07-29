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

    /// 设置读写偏移量。whence: 0=SEEK_SET, 1=SEEK_CUR, 2=SEEK_END。
    /// 返回新的绝对偏移量;默认不支持(返回 -1)。
    fn seek(&self, _offset: isize, _whence: usize) -> isize {
        -1
    }

    /// 把已打开的文件截断为 len 字节。默认不支持(返回 -1)。
    fn ftruncate(&self, _len: usize) -> isize {
        -1
    }

    /// 把 fd 指向的文件数据刷盘(落盘)。默认不支持(返回 -1)。
    fn fsync(&self) -> isize {
        -1
    }

    fn on_fork(&self) {}
    fn on_close_kind(&self) -> PipeCloseKind {PipeCloseKind::Nothing}
}