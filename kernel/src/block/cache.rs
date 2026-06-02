extern crate alloc;

use alloc::collections::VecDeque;
use alloc::sync::Arc;
use alloc::vec::Vec;

use crate::sync::spin::Mutex;

use super::BlockDevice;

pub const BLOCK_SIZE: usize = 512;
pub const BLOCK_CACHE_CAPACITY: usize = 64;

pub struct BlockCache {
    block_id: usize,
    data: [u8; BLOCK_SIZE],
    device: Arc<dyn BlockDevice>,
    modified: bool,
}

impl BlockCache {
    pub fn new(block_id: usize, device: Arc<dyn BlockDevice>) -> Self {
        let mut data = [0u8; BLOCK_SIZE];

        let ret = device.read_block(block_id, &mut data);
        assert_eq!(
            ret,
            BLOCK_SIZE as isize,
            "[block-cache] read block {} failed",
            block_id,
        );

        Self {
            block_id,
            data,
            device,
            modified: false,
        }
    }

    pub fn block_id(&self) -> usize {
        self.block_id
    }

    pub fn read_bytes(&self, offset: usize, buf: &mut [u8]) -> isize {
        let end = match offset.checked_add(buf.len()) {
            Some(end) => end,
            None => return -1,
        };

        if end > BLOCK_SIZE {
            return -1;
        }

        buf.copy_from_slice(&self.data[offset..end]);
        buf.len() as isize
    }

    pub fn read_u8(&self, offset: usize) -> u8 {
        assert!(offset < BLOCK_SIZE);
        self.data[offset]
    }

    pub fn read_u16(&self, offset: usize) -> u16 {
        assert!(offset + 2 <= BLOCK_SIZE);
        u16::from_le_bytes([
            self.data[offset],
            self.data[offset + 1],
        ])
    }

    pub fn read_u32(&self, offset: usize) -> u32 {
        assert!(offset + 4 <= BLOCK_SIZE);
        u32::from_le_bytes([
            self.data[offset],
            self.data[offset + 1],
            self.data[offset + 2],
            self.data[offset + 3],
        ])
    }

    pub fn read_u64(&self, offset: usize) -> u64 {
        assert!(offset + 8 <= BLOCK_SIZE);
        u64::from_le_bytes([
            self.data[offset],
            self.data[offset + 1],
            self.data[offset + 2],
            self.data[offset + 3],
            self.data[offset + 4],
            self.data[offset + 5],
            self.data[offset + 6],
            self.data[offset + 7],
        ])
    }

    pub fn write_bytes(&mut self, offset: usize, buf: &[u8]) -> isize {
        let end = match offset.checked_add(buf.len()) {
            Some(end) => end,
            None => return -1,
        };

        if end > BLOCK_SIZE {
            return -1;
        }

        self.data[offset..end].copy_from_slice(buf);
        self.modified = true;

        buf.len() as isize
    }

    pub fn sync(&mut self) {
        if self.modified {
            let ret = self.device.write_block(self.block_id, &self.data);
            assert_eq!(
                ret,
                BLOCK_SIZE as isize,
                "[block-cache] write block {} failed",
                self.block_id,
            );

            self.modified = false;
        }
    }
}

impl Drop for BlockCache {
    fn drop(&mut self) {
        self.sync();
    }
}

pub type BlockCacheRef = Arc<Mutex<BlockCache>>;

pub struct BlockCacheManager {
    queue: VecDeque<BlockCacheRef>,
}

impl BlockCacheManager {
    pub const fn new() -> Self {
        Self {
            queue: VecDeque::new(),
        }
    }

    pub fn get_block_cache(
        &mut self,
        block_id: usize,
        device: Arc<dyn BlockDevice>,
    ) -> BlockCacheRef {
        for cache in self.queue.iter() {
            if cache.lock().block_id() == block_id {
                return cache.clone();
            }
        }

        if self.queue.len() >= BLOCK_CACHE_CAPACITY {
            //第一版简单淘汰队首。
            //如果 Arc strong_count > 1，说明外面还有人在用，
            //那就先跳过，找一个没人用的 cache。
            let mut victim_index = None;

            for (i, cache) in self.queue.iter().enumerate() {
                if Arc::strong_count(cache) == 1 {
                    victim_index = Some(i);
                    break;
                }
            }

            if let Some(i) = victim_index {
                self.queue.remove(i);
            } else {
                panic!("[block-cache] no cache can be evicted");
            }
        }

        let cache = Arc::new(Mutex::new(BlockCache::new(block_id, device)));
        self.queue.push_back(cache.clone());
        cache
    }
}

static BLOCK_CACHE_MANAGER: Mutex<BlockCacheManager> =
    Mutex::new(BlockCacheManager::new());

pub fn get_block_cache(
    block_id: usize,
    device: Arc<dyn BlockDevice>,
) -> BlockCacheRef {
    BLOCK_CACHE_MANAGER
        .lock()
        .get_block_cache(block_id, device)
}