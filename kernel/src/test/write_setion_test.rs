use alloc::sync::Arc;
use crate::block::{BlockDevice, virtio_blk::VirtioBlkDevice};

pub fn test_write_read(dev: Arc<VirtioBlkDevice>) {

    let sector = 100_000;  

    let mut pattern = [0u8; 512];
    for i in 0..512 {
        pattern[i] = (i as u8) ^ 0x5a;  
    }


    let mut orig = [0u8; 512];
    dev.read_block(sector, &mut orig);


    let w = dev.write_block(sector, &pattern);
    log::info!("[virtio-blk][test] write ret={}", w);


    let mut readback = [0u8; 512];
    dev.read_block(sector, &mut readback);

    if readback == pattern {
        log::info!("[virtio-blk][test] WRITE-READ OK: data matches");
    } else {
        log::error!("[virtio-blk][test] MISMATCH! write path broken");
        log::error!("  first bytes: w={:02x} r={:02x}", pattern[0], readback[0]);
    }
}