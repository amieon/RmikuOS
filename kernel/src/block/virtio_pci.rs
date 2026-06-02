use crate::block::device::BlockDevice;
use crate::pci::ecam::{
    PciAddress,
    read_config_u8,
    read_config_u16,
    read_config_u32,
};
use crate::pci::bar::read_bar;

const PCI_CAPABILITY_LIST: usize = 0x34;
const PCI_CAP_ID_VENDOR_SPECIFIC: u8 = 0x09;

const VIRTIO_PCI_CAP_COMMON_CFG: u8 = 1;
const VIRTIO_PCI_CAP_NOTIFY_CFG: u8 = 2;
const VIRTIO_PCI_CAP_ISR_CFG: u8 = 3;
const VIRTIO_PCI_CAP_DEVICE_CFG: u8 = 4;

#[derive(Clone, Copy, Debug)]
pub struct VirtioPciRegion {
    pub bar: u8,
    pub offset: u32,
    pub length: u32,
    pub pa: usize,
    pub va: usize,
}

#[derive(Clone, Copy, Debug)]
pub struct VirtioPciRegions {
    pub common: Option<VirtioPciRegion>,
    pub notify: Option<VirtioPciRegion>,
    pub isr: Option<VirtioPciRegion>,
    pub device: Option<VirtioPciRegion>,
    pub notify_off_multiplier: u32,
}

impl VirtioPciRegions {
    pub const fn new() -> Self {
        Self {
            common: None,
            notify: None,
            isr: None,
            device: None,
            notify_off_multiplier: 0,
        }
    }
}

fn make_region(addr: PciAddress, bar: u8, offset: u32, length: u32) -> VirtioPciRegion {
    let bar_base = read_bar(addr, bar) as usize;
    let pa = bar_base + offset as usize;
    let va = crate::mm::kernel_phys_to_virt(pa);

    VirtioPciRegion {
        bar,
        offset,
        length,
        pa,
        va,
    }
}

pub fn parse_virtio_pci_caps(addr: PciAddress) -> Option<VirtioPciRegions> {
    let mut regions = VirtioPciRegions::new();

    let mut cap_ptr = read_config_u8(addr, PCI_CAPABILITY_LIST) as usize;
    let mut depth = 0;

    while cap_ptr != 0 && depth < 32 {
        depth += 1;

        let cap_id = read_config_u8(addr, cap_ptr + 0);
        let next = read_config_u8(addr, cap_ptr + 1) as usize;

        if cap_id == PCI_CAP_ID_VENDOR_SPECIFIC {
            let cap_len = read_config_u8(addr, cap_ptr + 2);
            let cfg_type = read_config_u8(addr, cap_ptr + 3);
            let bar = read_config_u8(addr, cap_ptr + 4);
            let offset = read_config_u32(addr, cap_ptr + 8);
            let length = read_config_u32(addr, cap_ptr + 12);

            log::info!(
                "[virtio-pci] cap at {:#x}: len={}, type={}, bar={}, offset={:#x}, length={:#x}",
                cap_ptr,
                cap_len,
                cfg_type,
                bar,
                offset,
                length,
            );

            match cfg_type {
                VIRTIO_PCI_CAP_COMMON_CFG => {
                    let r = make_region(addr, bar, offset, length);
                    log::info!("[virtio-pci] COMMON_CFG pa={:#x}, va={:#x}", r.pa, r.va);
                    regions.common = Some(r);
                }
                VIRTIO_PCI_CAP_NOTIFY_CFG => {
                    let r = make_region(addr, bar, offset, length);

                    /*
                     * notify capability 比基础 virtio_pci_cap 多一个 u32。
                     */
                    let multiplier = read_config_u32(addr, cap_ptr + 16);

                    log::info!(
                        "[virtio-pci] NOTIFY_CFG pa={:#x}, va={:#x}, multiplier={}",
                        r.pa,
                        r.va,
                        multiplier,
                    );

                    regions.notify = Some(r);
                    regions.notify_off_multiplier = multiplier;
                }
                VIRTIO_PCI_CAP_ISR_CFG => {
                    let r = make_region(addr, bar, offset, length);
                    log::info!("[virtio-pci] ISR_CFG pa={:#x}, va={:#x}", r.pa, r.va);
                    regions.isr = Some(r);
                }
                VIRTIO_PCI_CAP_DEVICE_CFG => {
                    let r = make_region(addr, bar, offset, length);
                    log::info!("[virtio-pci] DEVICE_CFG pa={:#x}, va={:#x}", r.pa, r.va);
                    regions.device = Some(r);
                }
                _ => {}
            }
        }

        cap_ptr = next;
    }

    if regions.common.is_none() || regions.notify.is_none() || regions.isr.is_none() {
        log::error!(
            "[virtio-pci] missing required caps: common={:?}, notify={:?}, isr={:?}, device={:?}",
            regions.common,
            regions.notify,
            regions.isr,
            regions.device,
        );
        return None;
    }

    Some(regions)
}