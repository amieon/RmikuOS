pub trait BlockDevice: Send + Sync {
    fn block_size(&self) -> usize;

    fn num_blocks(&self) -> usize;

    fn read_block(&self, block_id: usize, buf: &mut [u8]) -> isize;

    fn write_block(&self, block_id: usize, buf: &[u8]) -> isize;
}