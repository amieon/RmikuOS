use alloc::sync::Arc;
use crate::block::BlockDevice;

// 在 loongarch 初始化里,拿到 VirtioPciBlkDevice 后
pub fn test_pci_write_read(dev: Arc<crate::block::virtio_pci_blk::VirtioPciBlkDevice>) {
    let sector = 1000;   // 安全扇区(但注意别写坏 ext4!见下)
    let mut pattern = [0u8; 512];
    for i in 0..512 { pattern[i] = (i as u8) ^ 0x3c; }

    let w = dev.write_block(sector, &pattern);
    log::info!("[pci-test] write ret={}", w);

    let mut rb = [0u8; 512];
    dev.read_block(sector, &mut rb);

    if rb == pattern {
        log::info!("[pci-test] WRITE-READ OK on loongarch virtio-pci!");
    } else {
        log::error!("[pci-test] MISMATCH, pci write path broken");
    }
}