use core::ptr::{read_volatile, write_volatile};

#[derive(Clone, Copy, Debug)]
pub struct PciAddress {
    pub bus: u8,
    pub device: u8,
    pub function: u8,
}

impl PciAddress {
    pub const fn new(bus: u8, device: u8, function: u8) -> Self {
        Self {
            bus,
            device,
            function,
        }
    }
}

fn config_addr(addr: PciAddress, offset: usize) -> usize {
    /*
     * PCI ECAM:
     * bus      << 20
     * device   << 15
     * function << 12
     * register offset
     */
    let pa = crate::arch::PCI_ECAM_BASE
        + ((addr.bus as usize) << 20)
        + ((addr.device as usize) << 15)
        + ((addr.function as usize) << 12)
        + offset;

    crate::mm::kernel_phys_to_virt(pa)
}

pub fn read_config_u8(addr: PciAddress, offset: usize) -> u8 {
    unsafe {
        read_volatile(config_addr(addr, offset) as *const u8)
    }
}

pub fn read_config_u16(addr: PciAddress, offset: usize) -> u16 {
    assert!(offset % 2 == 0);

    unsafe {
        u16::from_le(read_volatile(config_addr(addr, offset) as *const u16))
    }
}

pub fn read_config_u32(addr: PciAddress, offset: usize) -> u32 {
    assert!(offset % 4 == 0);

    unsafe {
        u32::from_le(read_volatile(config_addr(addr, offset) as *const u32))
    }
}

pub fn write_config_u16(addr: PciAddress, offset: usize, value: u16) {
    assert!(offset % 2 == 0);

    unsafe {
        write_volatile(
            config_addr(addr, offset) as *mut u16,
            value.to_le(),
        );
    }
}

pub fn write_config_u32(addr: PciAddress, offset: usize, value: u32) {
    assert!(offset % 4 == 0);

    unsafe {
        write_volatile(
            config_addr(addr, offset) as *mut u32,
            value.to_le(),
        );
    }
}



const PCI_COMMAND: usize = 0x04;
const PCI_COMMAND_IO: u16 = 1 << 0;
const PCI_COMMAND_MEMORY: u16 = 1 << 1;
const PCI_COMMAND_BUS_MASTER: u16 = 1 << 2;

pub fn enable_pci_device(addr: PciAddress) {
    let old = read_config_u16(addr, PCI_COMMAND);
    let new = old | PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER;

    write_config_u16(addr, PCI_COMMAND, new);

    log::info!(
        "[pci] command {:#x}->{:#x}",
        old,
        new,
    );
}