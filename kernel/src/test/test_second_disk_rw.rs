use alloc::sync::Arc;
use crate::block::virtio_blk::VirtioBlkDevice;
use crate::block::BlockDevice;

pub fn test_second_disk_rw(dev: Arc<VirtioBlkDevice>) {
    let sector = 1000; 

    let mut pattern = [0u8; 512];
    for i in 0..512 {
        pattern[i] = (i as u8) ^ 0xa5;
    }

    let w = dev.write_block(sector, &pattern);
    log::info!("[disk2-test] write sector {} ret={}", sector, w);

    let mut readback = [0u8; 512];
    let r = dev.read_block(sector, &mut readback);
    log::info!("[disk2-test] read sector {} ret={}", sector, r);

    if readback == pattern {
        log::info!("[disk2-test] WRITE-READ OK on second disk!");
    } else {
        log::error!("[disk2-test] MISMATCH on second disk, write path broken");
        log::error!("  w[0]={:02x} r[0]={:02x}  w[1]={:02x} r[1]={:02x}",
            pattern[0], readback[0], pattern[1], readback[1]);
    }
}