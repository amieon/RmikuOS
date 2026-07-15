use alloc::sync::Arc;

use crate::drivers::block::{
    BlockDevice,
    RamDisk,
    get_block_cache,
};

static TEST_DISK: [u8; 1024] = {
    let mut data = [0u8; 1024];

    data[0] = b'R';
    data[1] = b'm';
    data[2] = b'i';
    data[3] = b'k';
    data[4] = b'u';
    data[5] = b'O';
    data[6] = b'S';

    data[512] = b'B';
    data[513] = b'L';
    data[514] = b'K';
    data[515] = b'1';

    data
};

pub fn test_block_cache() {
    let disk: Arc<dyn BlockDevice> =
        Arc::new(RamDisk::new(&TEST_DISK, 512));

    let cache0 = get_block_cache(0, disk.clone());
    let cache1 = get_block_cache(1, disk.clone());
    let cache0_again = get_block_cache(0, disk.clone());

    assert!(
        Arc::ptr_eq(&cache0, &cache0_again),
        "same block should return same cache object",
    );

    {
        let block0 = cache0.lock();
        assert_eq!(block0.read_u8(0), b'R');
        assert_eq!(block0.read_u8(1), b'm');
        assert_eq!(block0.read_u8(2), b'i');
    }

    {
        let block1 = cache1.lock();
        assert_eq!(block1.read_u8(0), b'B');
        assert_eq!(block1.read_u8(1), b'L');
        assert_eq!(block1.read_u8(2), b'K');
        assert_eq!(block1.read_u8(3), b'1');
    }

    log::info!("[block-cache] test passed");
}

