pub mod device;
pub mod ramdisk;
pub mod cache;
pub mod ext4_image;
pub mod blockio;
pub mod discover_disks;


pub use device::BlockDevice;
pub use ramdisk::RamDisk;


pub use cache::{
    BLOCK_SIZE,
    BlockCache,
    BlockCacheRef,
    CacheStats,
    fill_blocks,
    get_block_cache,
    log_stats,
    stats,
};


pub mod virtio_blk;
pub mod virtio_pci_blk;
pub mod virtio_blk_dma;