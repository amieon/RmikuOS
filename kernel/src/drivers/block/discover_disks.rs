use alloc::sync::Arc;
use super::BlockDevice;

/// 发现并初始化所有磁盘,按 ext4 magic 区分,返回 (ext4_rootfs, fat_disk)。
/// 两个都是 Option:找不到对应的盘就是 None。
pub fn discover_disks() -> (Option<Arc<dyn BlockDevice>>, Option<Arc<dyn BlockDevice>>) {
    #[cfg(target_arch = "riscv64")]
    {
        use super::virtio_blk::VirtioBlkDevice;

        let all = crate::drivers::virtio::transport::mmio::probe_all_virtio_blk_mmio();
        if all.is_empty() {
            log::warn!("[disk] no virtio-blk mmio found");
            return (None, None);
        }

        let mut ext4_dev: Option<Arc<dyn BlockDevice>> = None;
        let mut fat_dev: Option<Arc<dyn BlockDevice>> = None;

        for phys_base in all {
            let dev = match VirtioBlkDevice::init_from_phys_base(phys_base) {
                Some(d) => d,
                None => {
                    log::warn!("[disk] init virtio-blk at {:#x} failed, skip", phys_base);
                    continue;
                }
            };

            let mut buf = [0u8; 512];
            let ok = dev.read_block(2, &mut buf) == 512;
            let magic = if ok { u16::from_le_bytes([buf[56], buf[57]]) } else { 0 };

            if magic == 0xef53 {
                log::info!("[disk] disk at {:#x} is ext4", phys_base);
                ext4_dev = Some(dev as Arc<dyn BlockDevice>);
            } else {
                log::info!("[disk] disk at {:#x} is FAT candidate (magic={:#x})", phys_base, magic);
                fat_dev = Some(dev as Arc<dyn BlockDevice>);
            }
        }

        (ext4_dev, fat_dev)
    }

    #[cfg(target_arch = "loongarch64")]
    {
        use super::virtio_pci_blk::VirtioPciBlkDevice;

        crate::pci::scan_pci_bus();
        let all = crate::pci::find_all_virtio_blk_pci();
        if all.is_empty() {
            log::warn!("[disk] no virtio-pci blk found");
            return (None, None);
        }

        let mut ext4_dev: Option<Arc<dyn BlockDevice>> = None;
        let mut fat_dev: Option<Arc<dyn BlockDevice>> = None;
        // mmio_cursor 删掉

        for info in all {
            let addr = info.loc.addr();

            crate::pci::bar::assign_all_bars(addr);        // ← 原来是 ensure_mem_bar(addr, 4, mmio_cursor)
            crate::pci::ecam::enable_pci_device(addr);

            let regions = match crate::drivers::virtio::transport::pci::parse_virtio_pci_caps(addr) {
                Some(r) => r,
                None => {
                    log::warn!("[disk] parse caps failed, skip");
                    continue;
                }
            };
            let dev = match VirtioPciBlkDevice::init(regions) {
                Some(d) => d,
                None => {
                    log::warn!("[disk] init virtio-pci-blk failed, skip");
                    continue;
                }
            };

            let mut buf = [0u8; 512];
            let ok = dev.read_block(2, &mut buf) == 512;
            let magic = if ok { u16::from_le_bytes([buf[56], buf[57]]) } else { 0 };

            if magic == 0xef53 {
                log::info!("[disk] pci disk is ext4");
                ext4_dev = Some(dev as Arc<dyn BlockDevice>);
            } else {
                log::info!("[disk] pci disk is FAT candidate (magic={:#x})", magic);
                fat_dev = Some(dev as Arc<dyn BlockDevice>);
            }

            
        }

        (ext4_dev, fat_dev)
    }
}