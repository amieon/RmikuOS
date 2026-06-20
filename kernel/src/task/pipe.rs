pub const PIPE_BUF_SIZE: usize = 512;

pub struct Pipe {
    pub buf: [u8; PIPE_BUF_SIZE], 
    pub head: usize,
    pub tail: usize,
    pub len: usize,
    pub writer_count: usize,
    pub reader_count: usize,
}

pub struct PipeReadEnd  { pub inner: Arc<Mutex<Pipe>> }
pub struct PipeWriteEnd { pub inner: Arc<Mutex<Pipe>> }