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

fn read_device_info(loc: PciDeviceLocation) -> Option<PciDeviceInfo> {
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
    #[cfg(not(target_arch = "loongarch64"))]
    {
        log::warn!("[pci] scan only implemented for loongarch64 now");
    }

    #[cfg(target_arch = "loongarch64")]
    {
        log::info!("[pci] scan start");

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
}

pub fn find_virtio_blk_pci() -> Option<PciDeviceInfo> {
    #[cfg(target_arch = "loongarch64")]
    {
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

    #[cfg(not(target_arch = "loongarch64"))]
    {
        None
    }
}

pub fn find_all_virtio_blk_pci() -> alloc::vec::Vec<PciDeviceInfo> {
    let mut found = alloc::vec::Vec::new();

    #[cfg(target_arch = "loongarch64")]
    {
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
    }

    found
}