use alloc::sync::Arc;
use alloc::vec::Vec;

use super::device::BlockDevice;

pub const LABEL_ROOT: &str = "RMikuOS-ROOT";
pub const LABEL_DATA: &str = "RMikuOS-DATA";

pub struct DiskSet {
    /// rootfs（只读 ext4）
    pub ext4_dev: Option<Arc<dyn BlockDevice>>,
    /// FAT 盘
    pub fat_dev: Option<Arc<dyn BlockDevice>>,
    /// 可写数据盘（rsext4）
    pub data_dev: Option<Arc<dyn BlockDevice>>,
}

struct Probe {
    /// 扇区 2 偏移 56 的 ext4 魔数（0xef53）
    ext4_magic: u16,
    /// 扇区 2 偏移 120 起 16 字节卷标（去尾部零）
    label: &'static str, // 见下方 label_of 的说明，用 &str 生命周期挂在 'static 常量上
    /// 扇区 0 偏移 510 的 FAT 引导签名
    fat_sig: bool,
    /// 整个探测块全零（全新空盘，待内核 mkfs）
    blank: bool,
}

fn probe(dev: &Arc<dyn BlockDevice>) -> Probe {
    let mut buf2 = [0u8; 512];
    let ok2 = dev.read_block(2, &mut buf2) == 512;
    let ext4_magic = if ok2 {
        u16::from_le_bytes([buf2[56], buf2[57]])
    } else {
        0
    };
    // s_volume_name 在超级块偏移 120；超级块起始于字节 1024 = 扇区 2 开头
    let raw_label: &[u8] = if ok2 { &buf2[120..136] } else { &[] };
    let label = match_raw_label(raw_label);
    let blank = ok2 && buf2.iter().all(|&b| b == 0);

    let mut buf0 = [0u8; 512];
    let ok0 = dev.read_block(0, &mut buf0) == 512;
    let fat_sig = ok0 && buf0[510] == 0x55 && buf0[511] == 0xAA;

    Probe { ext4_magic, label, fat_sig, blank }
}

/// 把盘上读出的卷标匹配到已知的常量名；未知标签返回空串。
/// （返回 'static 是因为我们只跟常量比较，不持有盘上的数据）
fn match_raw_label(raw: &[u8]) -> &'static str {
    let raw = trim_zeros(raw);
    if raw == LABEL_ROOT.as_bytes() {
        LABEL_ROOT
    } else if raw == LABEL_DATA.as_bytes() {
        LABEL_DATA
    } else {
        ""
    }
}

fn trim_zeros(mut s: &[u8]) -> &[u8] {
    while let Some((&last, rest)) = s.split_last() {
        if last == 0 {
            s = rest;
        } else {
            break;
        }
    }
    s
}

fn classify(devices: Vec<Arc<dyn BlockDevice>>) -> DiskSet {
    let mut root: Option<Arc<dyn BlockDevice>> = None;
    let mut fat: Option<Arc<dyn BlockDevice>> = None;
    let mut data: Option<Arc<dyn BlockDevice>> = None;
    let mut leftover: Vec<Arc<dyn BlockDevice>> = Vec::new();

    // 第一遍：按卷标 / FAT 签名认领
    for (i, d) in devices.iter().enumerate() {
        let p = probe(d);
        log::info!(
            "[disk] #{} ({} 块, ext4_magic={:#x}, label=\"{}\"{}{})",
            i,
            d.num_blocks(),
            p.ext4_magic,
            p.label,
            if p.fat_sig { ", FAT签名" } else { "" },
            if p.blank { ", 空盘" } else { "" }
        );

        if !p.label.is_empty() {
            let slot = match p.label {
                LABEL_ROOT => &mut root,
                _ => &mut data,
            };
            if slot.is_some() {
                log::warn!("[disk] #{} 卷标 {} 重复，降级为顺序兜底", i, p.label);
                leftover.push(d.clone());
            } else {
                *slot = Some(d.clone());
            }
        } else if p.fat_sig && fat.is_none() {
            fat = Some(d.clone());
        } else {
            leftover.push(d.clone());
        }
    }

    // 第二遍：剩余盘按顺序兜底填空位；空盘优先给 data
    for d in leftover {
        let p = probe(&d);
        if p.blank && data.is_none() {
            log::info!("[disk] 空盘按兜底规则认作数据盘");
            data = Some(d);
            continue;
        }
        if root.is_none() {
            log::warn!("[disk] 无卷标盘按顺序兜底为 rootfs");
            root = Some(d);
        } else if fat.is_none() {
            log::warn!("[disk] 无卷标盘按顺序兜底为 FAT");
            fat = Some(d);
        } else if data.is_none() {
            log::warn!("[disk] 无卷标盘按顺序兜底为数据盘");
            data = Some(d);
        } else {
            log::warn!("[disk] 多余的盘，忽略");
        }
    }

    DiskSet { ext4_dev: root, fat_dev: fat, data_dev: data }
}

pub fn discover_disks() -> DiskSet {
    #[cfg(target_arch = "riscv64")]
    {
        use super::virtio_blk::VirtioBlkDevice;

        let all = crate::drivers::virtio::transport::mmio::probe_all_virtio_blk_mmio();
        if all.is_empty() {
            log::warn!("[disk] no virtio-blk mmio found");
            return DiskSet { ext4_dev: None, fat_dev: None, data_dev: None };
        }

        let mut devices: Vec<Arc<dyn BlockDevice>> = Vec::new();
        for phys_base in all {
            match VirtioBlkDevice::init_from_phys_base(phys_base) {
                Some(d) => devices.push(d as Arc<dyn BlockDevice>),
                None => {
                    log::warn!("[disk] init virtio-blk at {:#x} failed, skip", phys_base);
                }
            }
        }
        classify(devices)
    }

    #[cfg(target_arch = "loongarch64")]
    {
        use super::virtio_pci_blk::VirtioPciBlkDevice;

        crate::pci::scan_pci_bus();
        let all = crate::pci::find_all_virtio_blk_pci();
        if all.is_empty() {
            log::warn!("[disk] no virtio-pci blk found");
            return DiskSet { ext4_dev: None, fat_dev: None, data_dev: None };
        }

        let mut devices: Vec<Arc<dyn BlockDevice>> = Vec::new();
        for info in all {
            let addr = info.loc.addr();

            crate::pci::bar::assign_all_bars(addr);
            crate::pci::ecam::enable_pci_device(addr);

            let regions = match crate::drivers::virtio::transport::pci::parse_virtio_pci_caps(addr)
            {
                Some(r) => r,
                None => {
                    log::warn!("[disk] parse caps failed, skip");
                    continue;
                }
            };
            match VirtioPciBlkDevice::init(regions) {
                Some(d) => devices.push(d as Arc<dyn BlockDevice>),
                None => {
                    log::warn!("[disk] init virtio-pci-blk failed, skip");
                }
            }
        }
        classify(devices)
    }
}
