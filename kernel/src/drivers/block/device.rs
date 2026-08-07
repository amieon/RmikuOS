pub trait BlockDevice: Send + Sync {
    fn block_size(&self) -> usize;

    fn num_blocks(&self) -> usize;

    fn read_block(&self, block_id: usize, buf: &mut [u8]) -> isize;

    fn write_block(&self, block_id: usize, buf: &[u8]) -> isize;

    /// 把设备上所有写缓存刷盘。默认无操作(直通设备不需要)。
    /// 带写缓存的硬件设备应重写此方法,发出真正的 flush 命令。
    fn flush(&self) -> isize {
        0
    }
}