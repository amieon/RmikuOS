use core::ptr::{read_volatile, write_volatile};

const VIRTIO_MMIO_MAGIC_VALUE: usize = 0x000;
const VIRTIO_MMIO_VERSION: usize = 0x004;
const VIRTIO_MMIO_DEVICE_ID: usize = 0x008;
const VIRTIO_MMIO_VENDOR_ID: usize = 0x00c;

const VIRTIO_MMIO_DEVICE_FEATURES: usize = 0x010;
const VIRTIO_MMIO_DEVICE_FEATURES_SEL: usize = 0x014;
const VIRTIO_MMIO_DRIVER_FEATURES: usize = 0x020;
const VIRTIO_MMIO_DRIVER_FEATURES_SEL: usize = 0x024;

const VIRTIO_MMIO_QUEUE_SEL: usize = 0x030;
const VIRTIO_MMIO_QUEUE_SIZE_MAX: usize = 0x034;
const VIRTIO_MMIO_QUEUE_SIZE: usize = 0x038;
const VIRTIO_MMIO_QUEUE_READY: usize = 0x044;

const VIRTIO_MMIO_QUEUE_NOTIFY: usize = 0x050;
const VIRTIO_MMIO_INTERRUPT_STATUS: usize = 0x060;
const VIRTIO_MMIO_INTERRUPT_ACK: usize = 0x064;

const VIRTIO_MMIO_STATUS: usize = 0x070;

const VIRTIO_MMIO_QUEUE_DESC_LOW: usize = 0x080;
const VIRTIO_MMIO_QUEUE_DESC_HIGH: usize = 0x084;
const VIRTIO_MMIO_QUEUE_DRIVER_LOW: usize = 0x090;
const VIRTIO_MMIO_QUEUE_DRIVER_HIGH: usize = 0x094;
const VIRTIO_MMIO_QUEUE_DEVICE_LOW: usize = 0x0a0;
const VIRTIO_MMIO_QUEUE_DEVICE_HIGH: usize = 0x0a4;

pub const VIRTIO_MAGIC: u32 = 0x7472_6976;
pub const VIRTIO_DEVICE_ID_BLOCK: u32 = 2;

pub const VIRTIO_STATUS_ACKNOWLEDGE: u32 = 1;
pub const VIRTIO_STATUS_DRIVER: u32 = 2;
pub const VIRTIO_STATUS_DRIVER_OK: u32 = 4;
pub const VIRTIO_STATUS_FEATURES_OK: u32 = 8;
pub const VIRTIO_STATUS_FAILED: u32 = 128;


const VIRTIO_MMIO_GUEST_PAGE_SIZE: usize = 0x028; // legacy only
const VIRTIO_MMIO_QUEUE_ALIGN: usize = 0x03c;     // legacy only
const VIRTIO_MMIO_QUEUE_PFN: usize = 0x040;       // legacy only

/*
 * Modern virtio feature bit.
 * bit 32 -> feature word 1, bit 0.
 */
pub const VIRTIO_F_VERSION_1: u64 = 1u64 << 32;

#[derive(Clone, Copy)]
pub struct VirtioMmioHeader {
    base: usize, // kernel virtual address
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

    pub fn status(&self) -> u32 {
        self.read32(VIRTIO_MMIO_STATUS)
    }

    pub fn reset(&self) {
        self.write32(VIRTIO_MMIO_STATUS, 0);
        core::sync::atomic::fence(core::sync::atomic::Ordering::SeqCst);
    }

    pub fn set_status_bits(&self, bits: u32) {
        let status = self.status();
        self.write32(VIRTIO_MMIO_STATUS, status | bits);
        core::sync::atomic::fence(core::sync::atomic::Ordering::SeqCst);
    }

    pub fn fail(&self) {
        self.set_status_bits(VIRTIO_STATUS_FAILED);
    }

    pub fn read_device_features(&self, sel: u32) -> u32 {
        self.write32(VIRTIO_MMIO_DEVICE_FEATURES_SEL, sel);
        core::sync::atomic::fence(core::sync::atomic::Ordering::SeqCst);
        self.read32(VIRTIO_MMIO_DEVICE_FEATURES)
    }

    pub fn write_driver_features(&self, sel: u32, features: u32) {
        self.write32(VIRTIO_MMIO_DRIVER_FEATURES_SEL, sel);
        core::sync::atomic::fence(core::sync::atomic::Ordering::SeqCst);
        self.write32(VIRTIO_MMIO_DRIVER_FEATURES, features);
        core::sync::atomic::fence(core::sync::atomic::Ordering::SeqCst);
    }

    pub fn select_queue(&self, queue: u32) {
        self.write32(VIRTIO_MMIO_QUEUE_SEL, queue);
        core::sync::atomic::fence(core::sync::atomic::Ordering::SeqCst);
    }

    pub fn queue_size_max(&self) -> u32 {
        self.read32(VIRTIO_MMIO_QUEUE_SIZE_MAX)
    }

    pub fn queue_ready(&self) -> u32 {
        self.read32(VIRTIO_MMIO_QUEUE_READY)
    }

    pub fn set_queue_size(&self, size: u32) {
        self.write32(VIRTIO_MMIO_QUEUE_SIZE, size);
    }

    pub fn set_queue_ready(&self, ready: u32) {
        self.write32(VIRTIO_MMIO_QUEUE_READY, ready);
        core::sync::atomic::fence(core::sync::atomic::Ordering::SeqCst);
    }

    fn write64_to_pair(&self, low_off: usize, high_off: usize, value: u64) {
        self.write32(low_off, value as u32);
        self.write32(high_off, (value >> 32) as u32);
    }

    pub fn set_queue_desc_addr(&self, pa: usize) {
        self.write64_to_pair(
            VIRTIO_MMIO_QUEUE_DESC_LOW,
            VIRTIO_MMIO_QUEUE_DESC_HIGH,
            pa as u64,
        );
    }

    pub fn set_queue_driver_addr(&self, pa: usize) {
        self.write64_to_pair(
            VIRTIO_MMIO_QUEUE_DRIVER_LOW,
            VIRTIO_MMIO_QUEUE_DRIVER_HIGH,
            pa as u64,
        );
    }

    pub fn set_queue_device_addr(&self, pa: usize) {
        self.write64_to_pair(
            VIRTIO_MMIO_QUEUE_DEVICE_LOW,
            VIRTIO_MMIO_QUEUE_DEVICE_HIGH,
            pa as u64,
        );
    }

    pub fn notify_queue(&self, queue: u32) {
        self.write32(VIRTIO_MMIO_QUEUE_NOTIFY, queue);
    }

    pub fn interrupt_status(&self) -> u32 {
        self.read32(VIRTIO_MMIO_INTERRUPT_STATUS)
    }

    pub fn ack_interrupt(&self, bits: u32) {
        self.write32(VIRTIO_MMIO_INTERRUPT_ACK, bits);
    }

    pub fn set_guest_page_size(&self, size: u32) {
        self.write32(VIRTIO_MMIO_GUEST_PAGE_SIZE, size);
        core::sync::atomic::fence(core::sync::atomic::Ordering::SeqCst);
    }

    pub fn set_queue_align(&self, align: u32) {
        self.write32(VIRTIO_MMIO_QUEUE_ALIGN, align);
        core::sync::atomic::fence(core::sync::atomic::Ordering::SeqCst);
    }

    pub fn queue_pfn(&self) -> u32 {
        self.read32(VIRTIO_MMIO_QUEUE_PFN)
    }

    pub fn set_queue_pfn(&self, pfn: u32) {
        self.write32(VIRTIO_MMIO_QUEUE_PFN, pfn);
        core::sync::atomic::fence(core::sync::atomic::Ordering::SeqCst);
    }
}