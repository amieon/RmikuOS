
use crate::block::{BlockDevice, RamDisk};

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

pub fn test_ramdisk() {
    let disk = RamDisk::new(&TEST_DISK, 512);

    let mut block0 = [0u8; 512];
    let mut block1 = [0u8; 512];

    assert_eq!(disk.read_block(0, &mut block0), 512);
    assert_eq!(disk.read_block(1, &mut block1), 512);

    log::info!(
        "[block] ramdisk: size={} bytes, block_size={}, blocks={}",
        TEST_DISK.len(),
        disk.block_size(),
        disk.num_blocks(),
    );

    log::info!(
        "[block] block0 magic: {}{}{}{}{}{}{}",
        block0[0] as char,
        block0[1] as char,
        block0[2] as char,
        block0[3] as char,
        block0[4] as char,
        block0[5] as char,
        block0[6] as char,
    );

    log::info!(
        "[block] block1 magic: {}{}{}{}",
        block1[0] as char,
        block1[1] as char,
        block1[2] as char,
        block1[3] as char,
    );
}


