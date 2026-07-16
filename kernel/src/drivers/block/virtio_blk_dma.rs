use crate::mm::{
    kernel_phys_to_virt,
    PhysPageNum,
    PAGE_SIZE_BITS,
    config::PAGE_SIZE,
};
use crate::mm::frame_allocator::alloc_frame;

pub const VIRTIO_BLK_QUEUE_SIZE: usize = 8;

pub const VIRTIO_BLK_T_IN: u32 = 0;
pub const VIRTIO_BLK_T_OUT: u32 = 1;
pub const VIRTIO_BLK_S_OK: u8 = 0;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct VirtioBlkReq {
    pub req_type: u32,
    pub reserved: u32,
    pub sector: u64,
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