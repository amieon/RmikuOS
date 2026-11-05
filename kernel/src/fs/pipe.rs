use core::isize;

use crate::sync::spin::Mutex;
use crate::task::{wake_pipe_writers,wake_pipe_readers};

use alloc::sync::Arc;
use alloc::vec::Vec;

pub const PIPE_BUF_SIZE: usize = 16 * 1024;

pub struct Pipe {
    pub buf: Vec<u8>,
    pub head: usize,
    pub tail: usize,
    pub len: usize,
    pub writer_count: usize,
    pub reader_count: usize,
}

impl Pipe {
    pub fn new() -> Self{
        let mut buf = Vec::with_capacity(PIPE_BUF_SIZE);
        buf.resize(PIPE_BUF_SIZE, 0);
        Pipe {
            buf,
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
            /* 到这一步"没数据可读、也没到 EOF"，本来要睡了。
             * 如果此刻有未屏蔽的信号挂着，就返回 EINTR 而不是继续睡 ——
             * 否则用户态永远等不到那个 handler 之后的逻辑。
             */
            if crate::task::signal_pending_now() {
                return -crate::syscall::errno::EINTR;
            }
            /* 不用 block_current_on_pipe_read()：drop(pipe) 到把线程登记成
             * Blocking 之间有窗口，writer 恰好在窗口里 wake_pipe_readers()
             * 的话这次唤醒就丢了，reader 永久睡死。改成定时让出，条件一律在下一轮循环
             * 重查，不依赖唤醒送没送到（和 TCP 的 recv_data_ex 同一套做法）。 */
            let _ = crate::task::sleep_current_and_run_next(1);
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

            /* 读者全关：先发 SIGPIPE，再返回 EPIPE。
             * 检查必须放在"缓冲有空位就写"之前。否则读者已经退出的管道
             * 照样能写进环形缓冲（对端没人读，写进去就永远丢了），
             * `yes | head -1` 里 yes 会一直写下去而不是被杀掉。
             * 信号先于返回值：SIGPIPE 默认动作是终止进程，用户态根本看不到
             * 这个 -EPIPE；只有它被忽略/捕获时，调用方才拿到 EPIPE。
             */
            if pipe.reader_count == 0 {
                drop(pipe);
                crate::task::set_current_sig_pending(crate::task::SIGPIPE);
                return -crate::syscall::errno::EPIPE;
            }

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
            drop(pipe);
            /* 同 read 侧：定时让出 + 下一轮重查，避免丢唤醒后永久睡死。 */
            let _ = crate::task::sleep_current_and_run_next(1);
        }
    }
}

pub fn make_pipe() -> (FileRef, FileRef){
    let inner = Arc::new(Mutex::new(Pipe::new()));
    (Arc::new(PipeReadEnd{inner : inner.clone()}),Arc::new(PipeWriteEnd{inner : inner.clone()}))
}



