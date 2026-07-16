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

pub fn desc_size(queue_size: usize) -> usize {
    16 * queue_size
}

pub fn avail_size(queue_size: usize) -> usize {
    2 + 2 + 2 * queue_size + 2
}

pub fn used_size(queue_size: usize) -> usize {
    2 + 2 + 8 * queue_size
}

const fn align_up(value: usize, align: usize) -> usize {
    (value + align - 1) & !(align - 1)
}

pub fn modern_used_offset(queue_size: usize) -> usize {
    align_up(desc_size(queue_size) + avail_size(queue_size), 4)
}

pub fn legacy_used_offset(queue_size: usize) -> usize {
    align_up(desc_size(queue_size) + avail_size(queue_size), PAGE_SIZE)
}

pub struct VirtioQueue {
    pub queue_ppn: PhysPageNum,
    pub queue_pa: usize,
    pub queue_va: usize,
    pub pages: usize,
    pub desc_pa: usize,
    pub avail_pa: usize,
    pub used_pa: usize,
    pub size: usize,
}

impl VirtioQueue {
    fn new_with_layout(pages: usize, used_offset: usize, queue_size: usize) -> Option<Self> {
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
            desc_pa: pa,
            avail_pa: pa + desc_size(queue_size),
            used_pa: pa + used_offset,
            size: queue_size,
        })
    }

    pub fn new_legacy(queue_size: usize) -> Option<Self> {
        let desc = desc_size(queue_size);
        let avail = avail_size(queue_size);
        let used = used_size(queue_size);
        let total = desc + avail + used;
        let pages = (total + PAGE_SIZE - 1) / PAGE_SIZE;
        let pages = if pages < 2 { 2 } else { pages };
        Self::new_with_layout(pages, legacy_used_offset(queue_size), queue_size)
    }

    pub fn new_modern(queue_size: usize) -> Option<Self> {
        let desc = desc_size(queue_size);
        let avail = avail_size(queue_size);
        let used = used_size(queue_size);
        let total = desc + avail + used;
        let pages = (total + PAGE_SIZE - 1) / PAGE_SIZE;
        Self::new_with_layout(pages, modern_used_offset(queue_size), queue_size)
    }

    pub fn desc_va(&self) -> usize {
        self.queue_va
    }

    pub fn avail_va(&self) -> usize {
        self.queue_va + (self.avail_pa - self.queue_pa)
    }

    pub fn used_va(&self) -> usize {
        self.queue_va + (self.used_pa - self.queue_pa)
    }

    pub unsafe fn desc_mut(&self, index: usize) -> *mut VirtqDesc {
        assert!(index < self.size);
        (self.desc_va() as *mut VirtqDesc).add(index)
    }

    pub unsafe fn avail_flags_ptr(&self) -> *mut u16 {
        self.avail_va() as *mut u16
    }

    pub unsafe fn avail_idx_ptr(&self) -> *mut u16 {
        (self.avail_va() + 2) as *mut u16
    }

    pub unsafe fn avail_ring_ptr(&self, index: usize) -> *mut u16 {
        assert!(index < self.size);
        ((self.avail_va() + 4) as *mut u16).add(index)
    }

    pub unsafe fn used_idx_ptr(&self) -> *const u16 {
        (self.used_va() + 2) as *const u16
    }

    pub unsafe fn used_ring_ptr(&self, index: usize) -> *const VirtqUsedElem {
        assert!(index < self.size);
        ((self.used_va() + 4) as *const VirtqUsedElem).add(index)
    }
}