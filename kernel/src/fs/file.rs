use alloc::sync::Arc;

use crate::fs::stat::{Stat, STAT_TYPE_DIR, STAT_TYPE_FILE};

pub type FileRef = Arc<dyn File>;


/// 管道、普通文件、socket 共用
pub const POLLIN: i16 = 0x0001;   // 有数据可读,或读侧已到 EOF
pub const POLLOUT: i16 = 0x0004;  // 至少还能写入一个字节
pub const POLLERR: i16 = 0x0008;  // 错误(如管道读端全关闭后写)
pub const POLLHUP: i16 = 0x0010;  // 对端挂断(保留给 TCP/管道语义)
pub const POLLNVAL: i16 = 0x0020; // fd 无效;poll 不把它算进 events

pub enum PipeCloseKind {
    Nothing,
    ReaderGone,
    WriterGone,
}

pub trait File: Send + Sync {
    fn readable(&self) -> bool;
    fn writable(&self) -> bool;

    fn poll_ready(&self, events: i16) -> i16 {
        match self.stat().file_type {
            STAT_TYPE_FILE | STAT_TYPE_DIR => {
                let mut revents = 0;
                if events & POLLIN != 0 && self.readable() {
                    revents |= POLLIN;
                }
                if events & POLLOUT != 0 && self.writable() {
                    revents |= POLLOUT;
                }
                revents
            }
            _ => 0,
        }
    }

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
    fn socket_slot(&self) -> Option<usize> { None }
}