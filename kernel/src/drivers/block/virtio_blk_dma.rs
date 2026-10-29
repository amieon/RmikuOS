use crate::mm::{
    kernel_phys_to_virt,
    PhysPageNum,
    PAGE_SIZE_BITS,
    config::PAGE_SIZE,
};
use crate::mm::frame_allocator::alloc_contiguous_frames;

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

/// DMA 区总页数：第 0 页放控制结构，其余 16 页全给数据区。
///
/// 68 KiB 常驻（不释放，和队列一样），换一次请求能搬 128 个扇区 = 64 KiB。
/// 这是为了把"每 512 B 一次 ring 往返"变成"每 64 KiB 一次"——
/// TCG 下每次 notify 都是一次 vCPU 退出，次数少两个数量级才是真收益。
pub const DMA_PAGES: usize = 17;

const DMA_REQ_OFFSET: usize = 0;

/// status 字节紧跟 request 结构（16 字节）。
/// 旧布局把它压在数据区后面（offset 1024），导致数据区被夹在两块控制信息中间，
/// 一页里只剩 512 B 可用。
const DMA_STATUS_OFFSET: usize = 16;

/// 数据区从第 1 页开始：页对齐，调试时可以直接按页 hexdump。
const DMA_DATA_OFFSET: usize = PAGE_SIZE;

/// 数据区容量（字节）。
pub const DMA_DATA_CAPACITY: usize = (DMA_PAGES - 1) * PAGE_SIZE;

/// 单次请求最多能搬的扇区数（128 个 = 64 KiB）。
pub const DMA_MAX_SECTORS: usize = DMA_DATA_CAPACITY / 512;

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
        // 必须连续：一个 descriptor 只带一个 addr + len，设备会把整段当成
        // 线性缓冲区访问。页不连续不会报错，只会把数据写到别的物理页上。
        let ppn = alloc_contiguous_frames(DMA_PAGES)?;

        let pa = ppn.0 << PAGE_SIZE_BITS;
        let va = kernel_phys_to_virt(pa);

        unsafe {
            core::ptr::write_bytes(va as *mut u8, 0, DMA_PAGES * PAGE_SIZE);
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
