use alloc::sync::Arc;

use super::{
    get_block_cache,
    BlockDevice,
    RamDisk,
    BLOCK_SIZE,
};

// 评测模式(oscomp)不读盘,不需要嵌入 fs 镜像
#[cfg(not(feature = "oscomp"))]
#[cfg(target_arch = "loongarch64")]
static FS_IMG: &[u8] = include_bytes!("../../../target/fs-loongarch64.img");

#[cfg(not(feature = "oscomp"))]
#[cfg(target_arch = "riscv64")]
static FS_IMG: &[u8] = include_bytes!("../../../target/fs-riscv64.img");

#[cfg(feature = "oscomp")]
static FS_IMG: &[u8] = &[];


pub fn rootfs_image() -> &'static [u8] {
    FS_IMG
}

pub fn rootfs_ramdisk() -> Arc<dyn BlockDevice> {
    Arc::new(RamDisk::new(FS_IMG, BLOCK_SIZE))
}

pub fn test_ext4_magic() {
    let device = rootfs_ramdisk();


    // ext4 superblock 起始偏移是 1024。
    // magic 在 superblock 内偏移 0x38。
    
    // 总偏移：
    //   1024 + 0x38 = 1080
    
    // 如果底层 block size = 512：
    //   1080 / 512 = block 2
    //   1080 % 512 = 56

    let magic_abs_off = 1024 + 0x38;
    let block_id = magic_abs_off / BLOCK_SIZE;
    let offset = magic_abs_off % BLOCK_SIZE;

    let cache = get_block_cache(block_id, device.clone());
    let block = cache.lock();

    let magic = block.read_u16(offset);

    log::info!(
        "[ext4-image] image size={} KiB, blocks={}, magic={:#x}",
        FS_IMG.len() / 1024,
        device.num_blocks(),
        magic,
    );

    assert_eq!(
        magic,
        0xef53,
        "[ext4-image] bad ext4 magic"
    );
}