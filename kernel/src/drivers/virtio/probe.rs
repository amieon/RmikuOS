use alloc::vec::Vec;

use crate::pci::probe::{PciDeviceInfo, find_virtio_blk_pci as find_pci_blk};
use crate::drivers::virtio::transport::mmio::{
    probe_virtio_mmio,
    VIRTIO_DEVICE_ID_BLOCK,
    VIRTIO_DEVICE_ID_NETWORK,
};

pub fn find_virtio_blk_mmio() -> Vec<usize> {
    probe_virtio_mmio(VIRTIO_DEVICE_ID_BLOCK)
}

pub fn find_virtio_net_mmio() -> Vec<usize> {
    probe_virtio_mmio(VIRTIO_DEVICE_ID_NETWORK)
}

// PCI 探测：在 pci/probe.rs 里加这个，或者在这里封装
pub fn find_virtio_net_pci() -> Option<PciDeviceInfo> {
    #[cfg(target_arch = "loongarch64")]
    {
        use crate::pci::probe::{read_device_info, PciDeviceLocation};
        use crate::pci::ecam::read_config_u8;

        const PCI_DEVICE_ID_VIRTIO_NET_MODERN: u16 = 0x1041;
        const PCI_DEVICE_ID_VIRTIO_NET_TRANSITIONAL: u16 = 0x1000;

        for bus in 0u8..=0 {
            for device in 0u8..32 {
                for function in 0u8..8 {
                    let loc = PciDeviceLocation { bus, device, function };
                    let Some(info) = read_device_info(loc) else {
                        if function == 0 { break; }
                        continue;
                    };
                    if info.vendor_id == 0x1AF4
                        && (info.device_id == PCI_DEVICE_ID_VIRTIO_NET_MODERN
                            || info.device_id == PCI_DEVICE_ID_VIRTIO_NET_TRANSITIONAL)
                    {
                        return Some(info);
                    }
                    if function == 0 && (info.header_type & 0x80) == 0 {
                        break;
                    }
                }
            }
        }
    }
    None
}