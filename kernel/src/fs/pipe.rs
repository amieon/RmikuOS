use core::isize;

use crate::sync::spin::Mutex;
use crate::task::{block_current_on_pipe_read,block_current_on_pipe_write};
use crate::task::{wake_pipe_writers,wake_pipe_readers};

pub const PIPE_BUF_SIZE: usize = 512;

pub struct Pipe {
    pub buf: [u8; PIPE_BUF_SIZE], 
    pub head: usize,
    pub tail: usize,
    pub len: usize,
    pub writer_count: usize,
    pub reader_count: usize,
}

impl Pipe {
    pub fn new() -> Self{
        Pipe {
            buf: [0u8; PIPE_BUF_SIZE],
            head: 0,
            tail: 0,
            len: 0,
            writer_count: 1,
            reader_count: 1,
        }
    }
}

pub struct PipeReadEnd  { pub inner: Arc<Mutex<Pipe>> }
pub struct PipeWriteEnd { pub inner: Arc<Mutex<Pipe>> }

use alloc::sync::Arc;

use super::file::{File, FileRef, PipeCloseKind, POLLERR, POLLIN, POLLOUT};
use super::stat::*;


impl File for PipeReadEnd {
    fn readable(&self) -> bool {
        true
    }

    fn writable(&self) -> bool {
        false
    }

    /// 读端:缓冲非空可读;所有写端关闭后读 EOF,也算可读。
    fn poll_ready(&self, events: i16) -> i16 {
        let pipe = self.inner.lock();
        if events & POLLIN != 0 && (pipe.len != 0 || pipe.writer_count == 0) {
            POLLIN
        } else {
            0
        }
    }

    fn on_fork(&self){
        self.inner.lock().reader_count += 1;
    }

    fn on_close_kind(&self) -> super::file::PipeCloseKind {
        let mut pipe = self.inner.lock();
        pipe.reader_count -= 1;
        if pipe.reader_count == 0 { PipeCloseKind:: ReaderGone }
        else { PipeCloseKind::Nothing }
    }

    fn stat(&self) -> Stat {
        Stat::new(STAT_TYPE_PIPE, 0, 0o666, 0, 0)
    }

    fn read(&self, buf: &mut [u8]) -> isize {
        loop{
            let mut pipe = self.inner.lock();
            if pipe.len != 0 {
                let mut count = 0;
                while count < buf.len() && pipe.len > 0 {
                    buf[count] = pipe.buf[pipe.head];
                    pipe.head = (pipe.head + 1) % PIPE_BUF_SIZE;
                    pipe.len -= 1;
                    count += 1;
                }
                drop(pipe);       
                wake_pipe_writers();
                return count as isize;
            }
            if pipe.writer_count <= 0 {
                return super::EOF;
            }
            drop(pipe);              
            block_current_on_pipe_read();
        }
    }
    fn write(&self, _buf: &[u8]) -> isize {
        -1
    }
}


impl File for PipeWriteEnd {
    fn readable(&self) -> bool {
        false
    }

    fn writable(&self) -> bool {
        true
    }

    /// 写端:读端全关闭后写会 EPIPE,poll 报 POLLERR;
    /// 否则缓冲未满即可写(POLLOUT 只保证能写进至少一个字节)。
    fn poll_ready(&self, events: i16) -> i16 {
        let pipe = self.inner.lock();
        if pipe.reader_count == 0 {
            return POLLERR;
        }
        if events & POLLOUT != 0 && pipe.len < PIPE_BUF_SIZE {
            POLLOUT
        } else {
            0
        }
    }

    fn on_fork(&self){
        self.inner.lock().writer_count += 1;
    }
    
    fn on_close_kind(&self) -> super::file::PipeCloseKind {
        let mut pipe = self.inner.lock();
        pipe.writer_count -= 1;
        if pipe.writer_count == 0 { PipeCloseKind::WriterGone }
        else { PipeCloseKind::Nothing }
    }

    fn stat(&self) -> Stat {
        Stat::new(STAT_TYPE_PIPE, 0, 0o666, 0, 0)
    }

    fn read(&self, _buf: &mut [u8]) -> isize {
        -1
    }

    fn write(&self, buf: &[u8]) -> isize {
        loop{
            let mut pipe = self.inner.lock();
            if pipe.len != PIPE_BUF_SIZE {
                let mut count = 0;
                while count < buf.len() && pipe.len < PIPE_BUF_SIZE {
                    let tail = pipe.tail;    
                    pipe.buf[tail] = buf[count];
                    pipe.tail = (pipe.tail + 1) % PIPE_BUF_SIZE;
                    pipe.len += 1;
                    count += 1;
                }
                drop(pipe);       
                wake_pipe_readers();
                return count as isize;
            }
            if pipe.reader_count == 0 {
                return super::EPIPE;
            }
            drop(pipe);              
            block_current_on_pipe_write();
        }
    }
}

pub fn make_pipe() -> (FileRef, FileRef){
    let inner = Arc::new(Mutex::new(Pipe::new()));
    (Arc::new(PipeReadEnd{inner : inner.clone()}),Arc::new(PipeWriteEnd{inner : inner.clone()}))
}



