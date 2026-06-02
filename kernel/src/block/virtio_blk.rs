use alloc::sync::Arc;

use crate::mm::{
    kernel_phys_to_virt,
    PhysPageNum,
    PAGE_SIZE_BITS,
};
use crate::mm::config::PAGE_SIZE;
use crate::mm::frame_allocator::alloc_frame;

use super::virtio_mmio::{
    VirtioMmioHeader,
    VIRTIO_DEVICE_ID_BLOCK,
    VIRTIO_F_VERSION_1,
    VIRTIO_MAGIC,
    VIRTIO_STATUS_ACKNOWLEDGE,
    VIRTIO_STATUS_DRIVER,
    VIRTIO_STATUS_DRIVER_OK,
    VIRTIO_STATUS_FEATURES_OK,
};

/*
 * 第一版先用很小的 queue，够后面同步 read_block 使用。
 */
pub const VIRTIO_BLK_QUEUE_SIZE: usize = 8;

const DESC_SIZE: usize = 16 * VIRTIO_BLK_QUEUE_SIZE;

/*
 * split virtqueue avail ring:
 * flags: u16
 * idx:   u16
 * ring:  u16[QUEUE_SIZE]
 * used_event: u16  // 不启用 EVENT_IDX 也留出来
 */
const AVAIL_SIZE: usize = 2 + 2 + 2 * VIRTIO_BLK_QUEUE_SIZE + 2;

const fn align_up(value: usize, align: usize) -> usize {
    (value + align - 1) & !(align - 1)
}

const DESC_OFFSET: usize = 0;
const AVAIL_OFFSET: usize = DESC_OFFSET + DESC_SIZE;
const USED_OFFSET: usize = align_up(AVAIL_OFFSET + AVAIL_SIZE, 4);

/*
 * split virtqueue used ring:
 * flags: u16
 * idx:   u16
 * ring:  { id: u32, len: u32 }[QUEUE_SIZE]
 * avail_event: u16
 */
const USED_SIZE: usize = 2 + 2 + 8 * VIRTIO_BLK_QUEUE_SIZE + 2;

const QUEUE_MEM_SIZE: usize = USED_OFFSET + USED_SIZE;


use crate::mm::frame_allocator::alloc_contiguous_frames;


const LEGACY_USED_OFFSET: usize =
    align_up(AVAIL_OFFSET + AVAIL_SIZE, PAGE_SIZE);

const LEGACY_QUEUE_PAGES: usize = 2;

const MODERN_USED_OFFSET: usize =
    align_up(AVAIL_OFFSET + AVAIL_SIZE, 4);

const MODERN_QUEUE_PAGES: usize = 1;

pub struct VirtioQueue {
    pub queue_ppn: PhysPageNum,
    pub queue_pa: usize,
    pub queue_va: usize,
    pub pages: usize,

    pub desc_pa: usize,
    pub avail_pa: usize,
    pub used_pa: usize,
}

impl VirtioQueue {
    fn new_with_layout(pages: usize, used_offset: usize) -> Option<Self> {
        let ppn = alloc_contiguous_frames(pages)?;

        let pa = ppn.0 << PAGE_SIZE_BITS;
        let va = kernel_phys_to_virt(pa);

        unsafe {
            core::ptr::write_bytes(
                va as *mut u8,
                0,
                pages * PAGE_SIZE,
            );
        }

        Some(Self {
            queue_ppn: ppn,
            queue_pa: pa,
            queue_va: va,
            pages,

            desc_pa: pa + DESC_OFFSET,
            avail_pa: pa + AVAIL_OFFSET,
            used_pa: pa + used_offset,
        })
    }

    pub fn new_legacy() -> Option<Self> {
        Self::new_with_layout(LEGACY_QUEUE_PAGES, LEGACY_USED_OFFSET)
    }

    pub fn new_modern() -> Option<Self> {
        Self::new_with_layout(MODERN_QUEUE_PAGES, MODERN_USED_OFFSET)
    }
}
pub struct VirtioBlkDevice {
    pub phys_base: usize,
    pub virt_base: usize,
    pub header: VirtioMmioHeader,
    pub queue0: VirtioQueue,
}

impl VirtioBlkDevice {
    pub fn init_from_phys_base(phys_base: usize) -> Option<Arc<Self>> {
        let virt_base = crate::mm::kernel_phys_to_virt(phys_base);
        let header = VirtioMmioHeader::new(virt_base);

        if header.magic() != VIRTIO_MAGIC {
            log::error!(
                "[virtio-blk] bad magic at pa={:#x}: {:#x}",
                phys_base,
                header.magic(),
            );
            return None;
        }

        let version = header.version();

        if version != 1 && version != 2 {
            log::error!(
                "[virtio-blk] unsupported version at pa={:#x}: {}",
                phys_base,
                version,
            );
            return None;
        }

        if header.device_id() != VIRTIO_DEVICE_ID_BLOCK {
            log::error!(
                "[virtio-blk] not block device: device_id={}",
                header.device_id(),
            );
            return None;
        }

        log::info!(
            "[virtio-blk] init start: pa={:#x}, va={:#x}, version={}, vendor={:#x}",
            phys_base,
            virt_base,
            version,
            header.vendor_id(),
        );

        header.reset();

        header.set_status_bits(VIRTIO_STATUS_ACKNOWLEDGE);
        header.set_status_bits(VIRTIO_STATUS_DRIVER);

        let dev_features0 = header.read_device_features(0);
        let dev_features1 = if version == 2 {
            header.read_device_features(1)
        } else {
            0
        };

        log::info!(
            "[virtio-blk] device features: word0={:#x}, word1={:#x}",
            dev_features0,
            dev_features1,
        );

        if version == 2 {
            let version1_word =
                ((VIRTIO_F_VERSION_1 >> 32) & 0xffff_ffff) as u32;

            if (dev_features1 & version1_word) == 0 {
                log::error!("[virtio-blk] device does not offer VIRTIO_F_VERSION_1");
                header.fail();
                return None;
            }

            header.write_driver_features(0, 0);
            header.write_driver_features(1, version1_word);

            header.set_status_bits(VIRTIO_STATUS_FEATURES_OK);

            if (header.status() & VIRTIO_STATUS_FEATURES_OK) == 0 {
                log::error!("[virtio-blk] FEATURES_OK rejected by device");
                header.fail();
                return None;
            }
        } else {
            /*
            * legacy 第一版：不启用任何 feature。
            * 后面读块先走最小功能。
            */
            header.write_driver_features(0, 0);

            /*
            * legacy MMIO 需要告诉设备 guest page size。
            */
            header.set_guest_page_size(PAGE_SIZE as u32);
        }

        header.select_queue(0);

        let max = header.queue_size_max();

        if max == 0 {
            log::error!("[virtio-blk] queue 0 not available");
            header.fail();
            return None;
        }

        if max < VIRTIO_BLK_QUEUE_SIZE as u32 {
            log::error!(
                "[virtio-blk] queue 0 too small: max={}, need={}",
                max,
                VIRTIO_BLK_QUEUE_SIZE,
            );
            header.fail();
            return None;
        }

        let queue0 = if version == 1 {
            /*
            * legacy 判断 queue 是否已经在用，看 QueuePFN。
            */
            if header.queue_pfn() != 0 {
                log::error!(
                    "[virtio-blk] legacy queue 0 already in use, pfn={:#x}",
                    header.queue_pfn(),
                );
                header.fail();
                return None;
            }

            let q = match VirtioQueue::new_legacy() {
                Some(q) => q,
                None => {
                    log::error!("[virtio-blk] alloc legacy queue failed");
                    header.fail();
                    return None;
                }
            };

            header.set_queue_size(VIRTIO_BLK_QUEUE_SIZE as u32);
            header.set_queue_align(PAGE_SIZE as u32);

            /*
            * legacy QueuePFN = queue 物理地址 / PAGE_SIZE。
            */
            header.set_queue_pfn((q.queue_pa >> PAGE_SIZE_BITS) as u32);

            log::info!(
                "[virtio-blk] legacy queue0 ready: size={}, queue_pa={:#x}, pfn={:#x}, desc_pa={:#x}, avail_pa={:#x}, used_pa={:#x}",
                VIRTIO_BLK_QUEUE_SIZE,
                q.queue_pa,
                q.queue_pa >> PAGE_SIZE_BITS,
                q.desc_pa,
                q.avail_pa,
                q.used_pa,
            );

            q
        } else {
            if header.queue_ready() != 0 {
                log::error!("[virtio-blk] modern queue 0 already ready");
                header.fail();
                return None;
            }

            let q = match VirtioQueue::new_modern() {
                Some(q) => q,
                None => {
                    log::error!("[virtio-blk] alloc modern queue failed");
                    header.fail();
                    return None;
                }
            };

            header.set_queue_size(VIRTIO_BLK_QUEUE_SIZE as u32);
            header.set_queue_desc_addr(q.desc_pa);
            header.set_queue_driver_addr(q.avail_pa);
            header.set_queue_device_addr(q.used_pa);
            header.set_queue_ready(1);

            log::info!(
                "[virtio-blk] modern queue0 ready: size={}, desc_pa={:#x}, avail_pa={:#x}, used_pa={:#x}",
                VIRTIO_BLK_QUEUE_SIZE,
                q.desc_pa,
                q.avail_pa,
                q.used_pa,
            );

            q
        };

        header.set_status_bits(VIRTIO_STATUS_DRIVER_OK);

        log::info!(
            "[virtio-blk] init done: version={}, status={:#x}",
            version,
            header.status(),
        );

        Some(Arc::new(Self {
            phys_base,
            virt_base,
            header,
            queue0,
        }))
    }
}