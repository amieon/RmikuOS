use core::ptr::{read_volatile, write_volatile};

use crate::mm::{
    kernel_phys_to_virt,
    PhysPageNum,
    PAGE_SIZE_BITS,
};
use crate::mm::config::PAGE_SIZE;
use crate::mm::frame_allocator::{
    alloc_frame,
    alloc_contiguous_frames,
};

pub const VIRTIO_BLK_QUEUE_SIZE: usize = 8;

pub const VIRTQ_DESC_F_NEXT: u16 = 1;
pub const VIRTQ_DESC_F_WRITE: u16 = 2;

pub const VIRTQ_AVAIL_F_NO_INTERRUPT: u16 = 1;

pub const VIRTIO_BLK_T_IN: u32 = 0;
pub const VIRTIO_BLK_T_OUT: u32 = 1;
pub const VIRTIO_BLK_S_OK: u8 = 0;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct VirtqDesc {
    pub addr: u64,
    pub len: u32,
    pub flags: u16,
    pub next: u16,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct VirtqUsedElem {
    pub id: u32,
    pub len: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct VirtioBlkReq {
    pub req_type: u32,
    pub reserved: u32,
    pub sector: u64,
}

const DESC_SIZE: usize = 16 * VIRTIO_BLK_QUEUE_SIZE;
const AVAIL_SIZE: usize = 2 + 2 + 2 * VIRTIO_BLK_QUEUE_SIZE + 2;

pub const DESC_OFFSET: usize = 0;
pub const AVAIL_OFFSET: usize = DESC_OFFSET + DESC_SIZE;

const fn align_up(value: usize, align: usize) -> usize {
    (value + align - 1) & !(align - 1)
}

pub const MODERN_USED_OFFSET: usize =
    align_up(AVAIL_OFFSET + AVAIL_SIZE, 4);

pub const LEGACY_USED_OFFSET: usize =
    align_up(AVAIL_OFFSET + AVAIL_SIZE, PAGE_SIZE);

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
            core::ptr::write_bytes(va as *mut u8, 0, pages * PAGE_SIZE);
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
        Self::new_with_layout(2, LEGACY_USED_OFFSET)
    }

    pub fn new_modern() -> Option<Self> {
        Self::new_with_layout(1, MODERN_USED_OFFSET)
    }

    pub fn desc_va(&self) -> usize {
        self.queue_va + DESC_OFFSET
    }

    pub fn avail_va(&self) -> usize {
        self.queue_va + AVAIL_OFFSET
    }

    pub fn used_va(&self) -> usize {
        self.queue_va + (self.used_pa - self.queue_pa)
    }

    pub unsafe fn desc_mut(&self, index: usize) -> *mut VirtqDesc {
        assert!(index < VIRTIO_BLK_QUEUE_SIZE);
        (self.desc_va() as *mut VirtqDesc).add(index)
    }

    pub unsafe fn avail_flags_ptr(&self) -> *mut u16 {
        self.avail_va() as *mut u16
    }

    pub unsafe fn avail_idx_ptr(&self) -> *mut u16 {
        (self.avail_va() + 2) as *mut u16
    }

    pub unsafe fn avail_ring_ptr(&self, index: usize) -> *mut u16 {
        assert!(index < VIRTIO_BLK_QUEUE_SIZE);
        ((self.avail_va() + 4) as *mut u16).add(index)
    }

    pub unsafe fn used_idx_ptr(&self) -> *const u16 {
        (self.used_va() + 2) as *const u16
    }

    pub unsafe fn used_ring_ptr(&self, index: usize) -> *const VirtqUsedElem {
        assert!(index < VIRTIO_BLK_QUEUE_SIZE);
        ((self.used_va() + 4) as *const VirtqUsedElem).add(index)
    }
}

const DMA_REQ_OFFSET: usize = 0;
const DMA_DATA_OFFSET: usize = 512;
const DMA_STATUS_OFFSET: usize = DMA_DATA_OFFSET + 512;

pub struct VirtioBlkDma {
    pub ppn: PhysPageNum,
    pub pa: usize,
    pub va: usize,

    pub req_pa: usize,
    pub data_pa: usize,
    pub status_pa: usize,

    pub req_va: usize,
    pub data_va: usize,
    pub status_va: usize,
}

impl VirtioBlkDma {
    pub fn new() -> Option<Self> {
        let ppn = alloc_frame()?;

        let pa = ppn.0 << PAGE_SIZE_BITS;
        let va = kernel_phys_to_virt(pa);

        unsafe {
            core::ptr::write_bytes(va as *mut u8, 0, PAGE_SIZE);
        }

        Some(Self {
            ppn,
            pa,
            va,

            req_pa: pa + DMA_REQ_OFFSET,
            data_pa: pa + DMA_DATA_OFFSET,
            status_pa: pa + DMA_STATUS_OFFSET,

            req_va: va + DMA_REQ_OFFSET,
            data_va: va + DMA_DATA_OFFSET,
            status_va: va + DMA_STATUS_OFFSET,
        })
    }
}