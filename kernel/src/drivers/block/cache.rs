extern crate alloc;

use alloc::collections::{BTreeMap, VecDeque};
use alloc::sync::Arc;

use core::sync::atomic::{AtomicUsize, Ordering};

use crate::sync::spin::Mutex;

use super::BlockDevice;

pub const BLOCK_SIZE: usize = 512;

/// 常驻缓存块数（512 B/块）。
///
/// 8192 块 = 4 MiB，占 128 MiB 内核堆的 3%。
/// 原来只有 64 块（32 KiB）——比一个目录的块数组还小，顺序读大文件时
/// 命中率约等于 0，只是白付查找成本。
pub const BLOCK_CACHE_CAPACITY: usize = 8192;

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

/// 索引键：(设备身份, 块号)。
///
/// 不能只用块号——同时挂着 rootfs / FAT / data 三块盘时块号必然重叠。
/// 设备身份取 Arc 的分配地址（同一个 Arc clone 出来指针相同，不同设备必不同），
/// 这比在 trait 上加 id() 改动小，且不需要设备自己维护编号。
type CacheKey = (usize, usize);

fn cache_key(block_id: usize, device: &Arc<dyn BlockDevice>) -> CacheKey {
    (Arc::as_ptr(device) as *const () as usize, block_id)
}

struct Slot {
    cache: BlockCacheRef,
    /// CLOCK 的第二机会位：命中过就置位，淘汰扫到它时先清位、挪到队尾。
    referenced: bool,
}

/// 块缓存管理器。
///
/// 两个关键点（都是相对旧实现修的）：
///
/// * 查找从线性扫描改成树索引。旧实现每命中一个块都要 `cache.lock()`
///   遍历整个队列，64 项就是 64 次自旋锁；现在一次 B 树查找，零加锁。
///   （no_std 没有 HashMap，引入 hashbrown 只为一处查找不划算；
///   块号是密集整数，B 树的比较分支对缓存友好，8192 项也才 13 层。）
///
/// * 淘汰用 CLOCK 而不是纯 FIFO。旧实现按插入顺序淘汰，刚被高频访问的
///   元数据块（inode 表、块位图）一样会被踢掉。CLOCK 给被访问过的块一次
///   留任机会，逼近 LRU 但不需要维护链表。
pub struct BlockCacheManager {
    slots: BTreeMap<CacheKey, Slot>,
    /// CLOCK 环。只存键，不持有 Arc —— 否则 strong_count 永远 >= 2，
    /// 没有任何块能被判定为“无人使用”，淘汰会直接卡死。
    clock: VecDeque<CacheKey>,
}

impl BlockCacheManager {
    pub const fn new() -> Self {
        Self {
            slots: BTreeMap::new(),
            clock: VecDeque::new(),
        }
    }

    pub fn get_block_cache(
        &mut self,
        block_id: usize,
        device: Arc<dyn BlockDevice>,
    ) -> BlockCacheRef {
        let key = cache_key(block_id, &device);

        if let Some(slot) = self.slots.get_mut(&key) {
            slot.referenced = true;
            HITS.fetch_add(1, Ordering::Relaxed);
            return slot.cache.clone();
        }

        MISSES.fetch_add(1, Ordering::Relaxed);

        if self.slots.len() >= BLOCK_CACHE_CAPACITY {
            self.evict_one();
        }

        let cache: BlockCacheRef = Arc::new(Mutex::new(BlockCache::new(block_id, device)));

        // 新块 referenced 留 false：顺序读大文件时（ELF 加载）队头会立刻被
        // 回收，缓存不会因为“刚插进来的都算热”而白白多扫一圈。
        self.slots.insert(
            key,
            Slot {
                cache: cache.clone(),
                referenced: false,
            },
        );
        self.clock.push_back(key);

        cache
    }

    /// 淘汰一个块。优先选“这一圈没被访问过、且当前没人持有”的。
    ///
    /// `Arc::strong_count == 1` 说明只有 slots 自己拿着它——此时没法淘汰，
    /// 因为调用方可能正在 `read_bytes` 中间。这类块被跳过后挪到队尾再等一轮。
    fn evict_one(&mut self) {
        let rounds = self.clock.len();

        for _ in 0..rounds {
            let key = match self.clock.pop_front() {
                Some(key) => key,
                None => break,
            };

            let evictable = match self.slots.get_mut(&key) {
                Some(slot) => {
                    if slot.referenced {
                        slot.referenced = false;
                        false
                    } else {
                        Arc::strong_count(&slot.cache) == 1
                    }
                }
                None => true, // 悬空的键（理论上不会出现），顺手丢掉
            };

            if evictable {
                self.slots.remove(&key);
                EVICTIONS.fetch_add(1, Ordering::Relaxed);
                return;
            }

            self.clock.push_back(key);
        }

        panic!(
            "[block-cache] no cache can be evicted (all {} blocks pinned or hot)",
            self.slots.len(),
        );
    }

    pub fn resident(&self) -> usize {
        self.slots.len()
    }
}

static BLOCK_CACHE_MANAGER: Mutex<BlockCacheManager> =
    Mutex::new(BlockCacheManager::new());

static HITS: AtomicUsize = AtomicUsize::new(0);
static MISSES: AtomicUsize = AtomicUsize::new(0);
static EVICTIONS: AtomicUsize = AtomicUsize::new(0);

pub fn get_block_cache(
    block_id: usize,
    device: Arc<dyn BlockDevice>,
) -> BlockCacheRef {
    BLOCK_CACHE_MANAGER
        .lock()
        .get_block_cache(block_id, device)
}

/// 缓存统计快照。命中率是判断“扩容到底有没有意义”的唯一实测依据，
/// 别靠感觉调容量。
#[derive(Clone, Copy)]
pub struct CacheStats {
    pub hits: usize,
    pub misses: usize,
    pub evictions: usize,
    pub resident: usize,
}

impl CacheStats {
    pub fn total(&self) -> usize {
        self.hits + self.misses
    }

    /// 命中率（0..=100 的整数百分比）。
    pub fn hit_percent(&self) -> usize {
        let total = self.total();
        if total == 0 {
            0
        } else {
            self.hits * 100 / total
        }
    }
}

pub fn stats() -> CacheStats {
    CacheStats {
        hits: HITS.load(Ordering::Relaxed),
        misses: MISSES.load(Ordering::Relaxed),
        evictions: EVICTIONS.load(Ordering::Relaxed),
        resident: BLOCK_CACHE_MANAGER.lock().resident(),
    }
}

/// 打一行缓存统计日志。调用点应该是低频里程碑（不是每次 IO）。
pub fn log_stats(tag: &str) {
    let s = stats();
    log::info!(
        "[block-cache] {} hit={} miss={} ({}%) evict={} resident={}/{}",
        tag,
        s.hits,
        s.misses,
        s.hit_percent(),
        s.evictions,
        s.resident,
        BLOCK_CACHE_CAPACITY,
    );
}
