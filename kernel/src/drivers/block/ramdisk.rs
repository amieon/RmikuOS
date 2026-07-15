use super::device::BlockDevice;

pub struct RamDisk {
    data: &'static [u8],
    block_size: usize,
}

impl RamDisk {
    pub const fn new(data: &'static [u8], block_size: usize) -> Self {
        Self {
            data,
            block_size,
        }
    }

    pub fn read_bytes(&self, offset: usize, buf: &mut [u8]) -> isize {
        let end = match offset.checked_add(buf.len()) {
            Some(end) => end,
            None => return -1,
        };

        if end > self.data.len() {
            return -1;
        }

        buf.copy_from_slice(&self.data[offset..end]);

        buf.len() as isize
    }
}

impl BlockDevice for RamDisk {
    fn block_size(&self) -> usize {
        self.block_size
    }

    fn num_blocks(&self) -> usize {
        self.data.len() / self.block_size
    }

    fn read_block(&self, block_id: usize, buf: &mut [u8]) -> isize {
        if buf.len() != self.block_size {
            return -1;
        }

        let start = match block_id.checked_mul(self.block_size) {
            Some(start) => start,
            None => return -1,
        };

        let end = match start.checked_add(self.block_size) {
            Some(end) => end,
            None => return -1,
        };

        if end > self.data.len() {
            return -1;
        }

        buf.copy_from_slice(&self.data[start..end]);

        self.block_size as isize
    }

    fn write_block(&self, _block_id: usize, _buf: &[u8]) -> isize {
        /*
         * 第一版 ramdisk 只读。
         */
        -1
    }
}