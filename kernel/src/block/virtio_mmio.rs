use core::ptr::{read_volatile, write_volatile};

const VIRTIO_MMIO_MAGIC_VALUE: usize = 0x000;
const VIRTIO_MMIO_VERSION: usize = 0x004;
const VIRTIO_MMIO_DEVICE_ID: usize = 0x008;
const VIRTIO_MMIO_VENDOR_ID: usize = 0x00c;
const VIRTIO_MMIO_STATUS: usize = 0x070;

const VIRTIO_MAGIC: u32 = 0x7472_6976; // "virt"
const VIRTIO_DEVICE_ID_BLOCK: u32 = 2;

#[derive(Clone, Copy)]
pub struct VirtioMmioHeader {
    base: usize,
}

impl VirtioMmioHeader {
    pub const fn new(base: usize) -> Self {
        Self { base }
    }

    fn read32(&self, offset: usize) -> u32 {
        unsafe {
            read_volatile((self.base + offset) as *const u32)
        }
    }

    fn write32(&self, offset: usize, value: u32) {
        unsafe {
            write_volatile((self.base + offset) as *mut u32, value);
        }
    }

    pub fn magic(&self) -> u32 {
        self.read32(VIRTIO_MMIO_MAGIC_VALUE)
    }

    pub fn version(&self) -> u32 {
        self.read32(VIRTIO_MMIO_VERSION)
    }

    pub fn device_id(&self) -> u32 {
        self.read32(VIRTIO_MMIO_DEVICE_ID)
    }

    pub fn vendor_id(&self) -> u32 {
        self.read32(VIRTIO_MMIO_VENDOR_ID)
    }

    pub fn reset(&self) {
        self.write32(VIRTIO_MMIO_STATUS, 0);
    }
}