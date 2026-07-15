use alloc::sync::Arc;
use crate::drivers::block::BlockDevice;
use fatfs::{IoBase, Read, Write, Seek, SeekFrom};

const SECTOR_SIZE: usize = 512;

pub struct BlockIo {
    dev: Arc<dyn BlockDevice>,
    position: u64,
    size: u64,
}

impl BlockIo {
    pub fn new(dev: Arc<dyn BlockDevice>, num_sectors: u64) -> Self {
        Self {
            dev,
            position: 0,
            size: num_sectors * SECTOR_SIZE as u64,
        }
    }
}

impl IoBase for BlockIo {
    type Error = ();      // 直接用 () —— fatfs 已为 () 实现了 IoError
}

impl Read for BlockIo {
    fn read(&mut self, buf: &mut [u8]) -> Result<usize, ()> {
        if self.position >= self.size {
            return Ok(0);   // EOF
        }

        let remaining = (self.size - self.position) as usize;
        let to_read = buf.len().min(remaining);
        if to_read == 0 {
            return Ok(0);
        }

        let mut done = 0;
        let mut sector_buf = [0u8; SECTOR_SIZE];

        while done < to_read {
            let abs = self.position + done as u64;
            let sector = (abs / SECTOR_SIZE as u64) as usize;
            let off = (abs % SECTOR_SIZE as u64) as usize;

            if self.dev.read_block(sector, &mut sector_buf) != SECTOR_SIZE as isize {
                return Err(());
            }

            let chunk = (SECTOR_SIZE - off).min(to_read - done);
            buf[done..done + chunk].copy_from_slice(&sector_buf[off..off + chunk]);
            done += chunk;
        }

        self.position += done as u64;
        Ok(done)
    }
}

impl Write for BlockIo {
    fn write(&mut self, buf: &[u8]) -> Result<usize, ()> {
        if self.position >= self.size {
            return Ok(0);
        }

        let remaining = (self.size - self.position) as usize;
        let to_write = buf.len().min(remaining);
        if to_write == 0 {
            return Ok(0);
        }

        let mut done = 0;
        let mut sector_buf = [0u8; SECTOR_SIZE];

        while done < to_write {
            let abs = self.position + done as u64;
            let sector = (abs / SECTOR_SIZE as u64) as usize;
            let off = (abs % SECTOR_SIZE as u64) as usize;

            let chunk = (SECTOR_SIZE - off).min(to_write - done);

            if chunk == SECTOR_SIZE {
                // 整扇区写,直接覆盖
                sector_buf.copy_from_slice(&buf[done..done + chunk]);
            } else {
                // 非对齐:read-modify-write
                if self.dev.read_block(sector, &mut sector_buf) != SECTOR_SIZE as isize {
                    return Err(());
                }
                sector_buf[off..off + chunk].copy_from_slice(&buf[done..done + chunk]);
            }

            if self.dev.write_block(sector, &sector_buf) != SECTOR_SIZE as isize {
                return Err(());
            }

            done += chunk;
        }

        self.position += done as u64;
        Ok(done)
    }

    fn flush(&mut self) -> Result<(), ()> {
        Ok(())   // 无写缓存,每次 write 直接落盘
    }
}

impl Seek for BlockIo {
    fn seek(&mut self, pos: SeekFrom) -> Result<u64, ()> {
        let new_pos = match pos {
            SeekFrom::Start(n) => n as i64,
            SeekFrom::End(n) => self.size as i64 + n,
            SeekFrom::Current(n) => self.position as i64 + n,
        };

        if new_pos < 0 {
            return Err(());
        }

        self.position = new_pos as u64;
        Ok(self.position)
    }
}