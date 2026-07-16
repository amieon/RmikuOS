use super::ecam::{
    PciAddress,
    read_config_u8,
    read_config_u16,
    read_config_u32,
};

const PCI_VENDOR_ID_INVALID: u16 = 0xffff;

const PCI_VENDOR_ID_VIRTIO: u16 = 0x1af4;

/*
 * modern virtio PCI:
 * device id = 0x1040 + virtio device id
 * block device virtio id = 2
 * 所以 modern virtio-blk-pci device id = 0x1042。
 *
 * transitional/legacy virtio-blk 常见 device id 是 0x1001。
 */
const PCI_DEVICE_ID_VIRTIO_BLK_MODERN: u16 = 0x1042;
const PCI_DEVICE_ID_VIRTIO_BLK_TRANSITIONAL: u16 = 0x1001;

#[derive(Clone, Copy, Debug)]
pub struct PciDeviceLocation {
    pub bus: u8,
    pub device: u8,
    pub function: u8,
}

impl PciDeviceLocation {
    pub fn addr(&self) -> PciAddress {
        PciAddress::new(self.bus, self.device, self.function)
    }
}

#[derive(Clone, Copy, Debug)]
pub struct PciDeviceInfo {
    pub loc: PciDeviceLocation,
    pub vendor_id: u16,
    pub device_id: u16,
    pub class_code: u8,
    pub subclass: u8,
    pub prog_if: u8,
    pub header_type: u8,
}

pub fn read_device_info(loc: PciDeviceLocation) -> Option<PciDeviceInfo> {
    let addr = loc.addr();

    let vendor_id = read_config_u16(addr, 0x00);

    if vendor_id == PCI_VENDOR_ID_INVALID {
        return None;
    }

    let device_id = read_config_u16(addr, 0x02);
    let prog_if = read_config_u8(addr, 0x09);
    let subclass = read_config_u8(addr, 0x0a);
    let class_code = read_config_u8(addr, 0x0b);
    let header_type = read_config_u8(addr, 0x0e);

    Some(PciDeviceInfo {
        loc,
        vendor_id,
        device_id,
        class_code,
        subclass,
        prog_if,
        header_type,
    })
}

pub fn scan_pci_bus() {


    for bus in 0u8..=0 {
        for device in 0u8..32 {
            for function in 0u8..8 {
                let loc = PciDeviceLocation {
                    bus,
                    device,
                    function,
                };

                let Some(info) = read_device_info(loc) else {
                    if function == 0 {
                        break;
                    }
                    continue;
                };

                log::info!(
                    "[pci] bus={:02x} dev={:02x} func={} vendor={:#06x} device={:#06x} class={:02x}/{:02x}/{:02x} header={:#x}",
                    bus,
                    device,
                    function,
                    info.vendor_id,
                    info.device_id,
                    info.class_code,
                    info.subclass,
                    info.prog_if,
                    info.header_type,
                );

                /*
                    * 如果 function 0 不是 multifunction，就不用扫 function 1..7。
                    */
                if function == 0 && (info.header_type & 0x80) == 0 {
                    break;
                }
            }
        }
    }

    log::info!("[pci] scan done");

}

pub fn find_virtio_blk_pci() -> Option<PciDeviceInfo> {

    for bus in 0u8..=0 {
        for device in 0u8..32 {
            for function in 0u8..8 {
                let loc = PciDeviceLocation {
                    bus,
                    device,
                    function,
                };

                let Some(info) = read_device_info(loc) else {
                    if function == 0 {
                        break;
                    }
                    continue;
                };

                if info.vendor_id == PCI_VENDOR_ID_VIRTIO
                    && (
                        info.device_id == PCI_DEVICE_ID_VIRTIO_BLK_MODERN
                        || info.device_id == PCI_DEVICE_ID_VIRTIO_BLK_TRANSITIONAL
                    )
                {
                    log::info!(
                        "[pci] found virtio-blk-pci: bus={:02x} dev={:02x} func={} vendor={:#06x} device={:#06x}",
                        bus,
                        device,
                        function,
                        info.vendor_id,
                        info.device_id,
                    );
                    return Some(info);
                }

                if function == 0 && (info.header_type & 0x80) == 0 {
                    break;
                }
            }
        }
    }

    log::warn!("[pci] no virtio-blk-pci found");
    None
}

pub fn find_all_virtio_blk_pci() -> alloc::vec::Vec<PciDeviceInfo> {
    let mut found = alloc::vec::Vec::new();


    for bus in 0u8..=0 {
        for device in 0u8..32 {
            for function in 0u8..8 {
                let loc = PciDeviceLocation { bus, device, function };

                let Some(info) = read_device_info(loc) else {
                    if function == 0 {
                        break;
                    }
                    continue;
                };

                if info.vendor_id == PCI_VENDOR_ID_VIRTIO
                    && (info.device_id == PCI_DEVICE_ID_VIRTIO_BLK_MODERN
                        || info.device_id == PCI_DEVICE_ID_VIRTIO_BLK_TRANSITIONAL)
                {
                    log::info!(
                        "[pci] found virtio-blk-pci: bus={:02x} dev={:02x} func={} device={:#06x}",
                        bus, device, function, info.device_id,
                    );
                    found.push(info);    // 收集,不 return
                }

                if function == 0 && (info.header_type & 0x80) == 0 {
                    break;
                }
            }
        }
    }

    if found.is_empty() {
        log::warn!("[pci] no virtio-blk-pci found");
    } else {
        log::info!("[pci] found {} virtio-blk-pci device(s)", found.len());
    }
    

    found
}

use core::sync::atomic::{AtomicUsize, Ordering};
use super::bar::{read_bar,read_bar_raw,assign_mem_bar};
use super::ecam::write_config_u32;
/// LoongArch QEMU virt 的 PCI MMIO32 窗口
const MMIO_WIN_BASE: usize = 0x4000_0000;
const MMIO_WIN_END: usize = 0x8000_0000;
static NEXT_MMIO: AtomicUsize = AtomicUsize::new(MMIO_WIN_BASE);

/// BAR sizing：写全 1 读回算大小，0 = BAR 不存在
pub fn bar_size(addr: PciAddress, bar: u8) -> usize {
    let off = 0x10 + (bar as usize) * 4;
    let old = read_config_u32(addr, off);
    if old & 0x1 != 0 { return 0; }              // I/O BAR 不管
    write_config_u32(addr, off, 0xFFFF_FFFF);
    let mask = read_config_u32(addr, off);
    write_config_u32(addr, off, old);            // 恢复原值
    let size_bits = mask & !0xF;
    if size_bits == 0 { 0 } else { (!size_bits).wrapping_add(1) as usize }
}

/// 已分配就返回现地址，未分配就从窗口 bump 分配（BAR 地址必须按 size 对齐）
pub fn alloc_mem_bar(addr: PciAddress, bar: u8) -> Option<usize> {
    let old = read_bar(addr, bar);
    if old != 0 { return Some(old as usize); }
    let size = bar_size(addr, bar);
    if size == 0 { return None; }
    let cur = NEXT_MMIO.load(Ordering::Relaxed);
    let base = (cur + size - 1) & !(size - 1);
    if base + size > MMIO_WIN_END {
        log::warn!("[pci] BAR{}: MMIO 窗口耗尽 (size={:#x})", bar, size);
        return None;
    }
    assign_mem_bar(addr, bar, base);
    NEXT_MMIO.store(base + size, Ordering::Relaxed);
    Some(base)
}

/// 给设备所有 mem BAR 分地址（64-bit BAR 占两个槽，要跳）
pub fn assign_all_bars(addr: PciAddress) {
    let mut bar = 0u8;
    while bar < 6 {
        let lo = read_bar_raw(addr, bar);
        let is_64 = lo & 0x1 == 0 && (lo >> 1) & 0x3 == 0x2;
        let _ = alloc_mem_bar(addr, bar);
        bar += if is_64 { 2 } else { 1 };
    }
}