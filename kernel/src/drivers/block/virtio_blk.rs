use alloc::sync::Arc;

use crate::mm::{
    kernel_phys_to_virt,
    PhysPageNum,
    PAGE_SIZE_BITS,
};
use crate::mm::config::PAGE_SIZE;
use crate::mm::frame_allocator::alloc_frame;

use crate::drivers::virtio::transport::mmio::{
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

#[repr(C)]
#[derive(Clone, Copy)]
struct VirtioBlkReq {
    req_type: u32,
    reserved: u32,
    sector: u64,
}

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
    pub inner: crate::sync::spin::Mutex<VirtioBlkInner>,
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
            log::info!("[blk] queue pa={:#x} pages={}", q.queue_pa, q.pages);
            q
        };
        

        header.set_status_bits(VIRTIO_STATUS_DRIVER_OK);

        // log::info!(
        //     "[virtio-blk] init done: version={}, status={:#x}",
        //     version,
        //     header.status(),
        // );
        let dma = match VirtioBlkDma::new() {
            Some(dma) => dma,
            None => {
                log::error!("[virtio-blk] alloc dma page failed");
                header.fail();
                return None;
            }
        };
        Some(Arc::new(Self {
            phys_base,
            virt_base,
            header,
            inner: crate::sync::spin::Mutex::new(VirtioBlkInner {
                queue0,
                dma,
                avail_idx: 0,
                last_used_idx: 0,
            }),
        }))
    }
}



use core::ptr::{
    read_volatile,
    write_volatile,
};

const VIRTQ_DESC_F_NEXT: u16 = 1;
const VIRTQ_DESC_F_WRITE: u16 = 2;

const VIRTQ_AVAIL_F_NO_INTERRUPT: u16 = 1;

const VIRTIO_BLK_T_IN: u32 = 0;
const VIRTIO_BLK_T_OUT: u32 = 1;
const VIRTIO_BLK_S_OK: u8 = 0;

#[repr(C)]
#[derive(Clone, Copy)]
struct VirtqDesc {
    addr: u64,
    len: u32,
    flags: u16,
    next: u16,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct VirtqUsedElem {
    id: u32,
    len: u32,
}



impl VirtioQueue {
    fn desc_va(&self) -> usize {
        self.queue_va + DESC_OFFSET
    }

    fn avail_va(&self) -> usize {
        self.queue_va + AVAIL_OFFSET
    }

    fn used_va(&self) -> usize {
        /*
         * 如果你只用 legacy，现在 used_pa 是 queue_pa + LEGACY_USED_OFFSET。
         * 所以这里直接通过 pa 差值算 offset。
         */
        self.queue_va + (self.used_pa - self.queue_pa)
    }

    unsafe fn desc_mut(&self, index: usize) -> *mut VirtqDesc {
        assert!(index < VIRTIO_BLK_QUEUE_SIZE);
        (self.desc_va() as *mut VirtqDesc).add(index)
    }

    unsafe fn avail_flags_ptr(&self) -> *mut u16 {
        self.avail_va() as *mut u16
    }

    unsafe fn avail_idx_ptr(&self) -> *mut u16 {
        (self.avail_va() + 2) as *mut u16
    }

    unsafe fn used_idx_ptr(&self) -> *const u16 {
        (self.used_va() + 2) as *const u16
    }


    unsafe fn avail_ring_ptr(&self, index: usize) -> *mut u16 {
    assert!(index < VIRTIO_BLK_QUEUE_SIZE);
    ((self.avail_va() + 4) as *mut u16).add(index)
    }

    unsafe fn used_ring_ptr(&self, index: usize) -> *const VirtqUsedElem {
        assert!(index < VIRTIO_BLK_QUEUE_SIZE);
        ((self.used_va() + 4) as *const VirtqUsedElem).add(index)
    }
}


const DMA_REQ_OFFSET: usize = 0;
const DMA_DATA_OFFSET: usize = 512;
const DMA_STATUS_OFFSET: usize = DMA_DATA_OFFSET + 512;


pub struct VirtioBlkDma {
    ppn: PhysPageNum,
    pa: usize,
    va: usize,

    req_pa: usize,
    data_pa: usize,
    status_pa: usize,

    req_va: usize,
    data_va: usize,
    status_va: usize,
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

pub struct VirtioBlkInner {
    pub queue0: VirtioQueue,
    pub dma: VirtioBlkDma,
    pub avail_idx: u16,
    pub last_used_idx: u16,
}


impl VirtioBlkDevice {
    fn read_sector_sync(&self, sector: usize, out: &mut [u8]) -> isize {
        if out.len() != 512 {
            return -1;
        }

        let mut inner = self.inner.lock();

        /*
         * 先把会用到的指针和地址取出来，避免 borrow checker 卡住。
         */
        let desc0 = unsafe { inner.queue0.desc_mut(0) };
        let desc1 = unsafe { inner.queue0.desc_mut(1) };
        let desc2 = unsafe { inner.queue0.desc_mut(2) };

        let avail_flags = unsafe { inner.queue0.avail_flags_ptr() };
        let avail_idx_ptr = unsafe { inner.queue0.avail_idx_ptr() };

        let used_idx_ptr = unsafe { inner.queue0.used_idx_ptr() };

        let req_va = inner.dma.req_va;
        let data_va = inner.dma.data_va;
        let status_va = inner.dma.status_va;

        let req_pa = inner.dma.req_pa;
        let data_pa = inner.dma.data_pa;
        let status_pa = inner.dma.status_pa;

        let old_avail_idx = inner.avail_idx;
        let ring_index = (old_avail_idx as usize) % VIRTIO_BLK_QUEUE_SIZE;
        let avail_ring = unsafe { inner.queue0.avail_ring_ptr(ring_index) };

        unsafe {
            /*
             * 1. 准备 request header。
             */
            let req = VirtioBlkReq {
                req_type: VIRTIO_BLK_T_IN,
                reserved: 0,
                sector: sector as u64,
            };

            write_volatile(req_va as *mut VirtioBlkReq, req);

            /*
             * status 先写成 0xff，方便判断设备有没有真的写回来。
             */
            write_volatile(status_va as *mut u8, 0xff);

            /*
             * data buffer 清零，方便 debug。
             */
            core::ptr::write_bytes(data_va as *mut u8, 0, 512);

            /*
             * 2. descriptor chain:
             *
             * desc0: request header，device readable
             * desc1: data buffer，device writable
             * desc2: status byte，device writable
             */
            write_volatile(
                desc0,
                VirtqDesc {
                    addr: req_pa as u64,
                    len: core::mem::size_of::<VirtioBlkReq>() as u32,
                    flags: VIRTQ_DESC_F_NEXT,
                    next: 1,
                },
            );

            write_volatile(
                desc1,
                VirtqDesc {
                    addr: data_pa as u64,
                    len: 512,
                    flags: VIRTQ_DESC_F_NEXT | VIRTQ_DESC_F_WRITE,
                    next: 2,
                },
            );

            write_volatile(
                desc2,
                VirtqDesc {
                    addr: status_pa as u64,
                    len: 1,
                    flags: VIRTQ_DESC_F_WRITE,
                    next: 0,
                },
            );

            /*
             * 3. 放进 avail ring。
             */
            write_volatile(avail_flags, VIRTQ_AVAIL_F_NO_INTERRUPT);
            write_volatile(avail_ring, 0);

            core::sync::atomic::fence(core::sync::atomic::Ordering::SeqCst);

            let new_avail_idx = old_avail_idx.wrapping_add(1);
            inner.avail_idx = new_avail_idx;

            write_volatile(avail_idx_ptr, new_avail_idx);

            core::sync::atomic::fence(core::sync::atomic::Ordering::SeqCst);

            /*
             * 4. 通知设备。
             */
            self.header.notify_queue(0);

            /*
             * 5. 等设备更新 used ring。
             */
            let mut spin = 0usize;

            loop {
                let used_idx = read_volatile(used_idx_ptr);

                if used_idx != inner.last_used_idx {
                    break;
                }

                spin += 1;

                if spin > 10_000_000 {
                    log::error!(
                        "[virtio-blk] read sector {} timeout, avail_idx={}, used_idx={}, last_used_idx={}",
                        sector,
                        inner.avail_idx,
                        used_idx,
                        inner.last_used_idx,
                    );
                    return -1;
                }

                core::hint::spin_loop();
            }

            /*
             * 6. 读取 used elem。
             */
            let used_ring_index =
                (inner.last_used_idx as usize) % VIRTIO_BLK_QUEUE_SIZE;

            let used_ptr = inner.queue0.used_ring_ptr(used_ring_index);
            let used = read_volatile(used_ptr);

            inner.last_used_idx = inner.last_used_idx.wrapping_add(1);

            /*
             * 7. ack interrupt，可有可无，但加上更干净。
             */
            let isr = self.header.interrupt_status();
            if isr != 0 {
                self.header.ack_interrupt(isr);
            }

            /*
             * 8. 检查 used id 和 status。
             */
            let status = read_volatile(status_va as *const u8);

            // log::info!(
            //     "[virtio-blk] done sector={}, used.id={}, used.len={}, status={}",
            //     sector,
            //     used.id,
            //     used.len,
            //     status,
            // );

            if used.id != 0 {
                log::error!(
                    "[virtio-blk] unexpected used id: {}",
                    used.id,
                );
                return -1;
            }

            if status != VIRTIO_BLK_S_OK {
                log::error!(
                    "[virtio-blk] read sector {} failed, status={}",
                    sector,
                    status,
                );
                return -1;
            }

            /*
             * 9. 关键：把 DMA data buffer 复制到 out。
             */
            let data = core::slice::from_raw_parts(
                data_va as *const u8,
                512,
            );

            out.copy_from_slice(data);
        }

        512
    }
}

impl VirtioBlkDevice {
    fn write_sector_sync(&self, sector: usize, in_buf: &[u8]) -> isize {
        if in_buf.len() != 512 {
            return -1;
        }

        let mut inner = self.inner.lock();

        let desc0 = unsafe { inner.queue0.desc_mut(0) };
        let desc1 = unsafe { inner.queue0.desc_mut(1) };
        let desc2 = unsafe { inner.queue0.desc_mut(2) };

        let avail_flags = unsafe { inner.queue0.avail_flags_ptr() };
        let avail_idx_ptr = unsafe { inner.queue0.avail_idx_ptr() };
        let used_idx_ptr = unsafe { inner.queue0.used_idx_ptr() };

        let req_va = inner.dma.req_va;
        let data_va = inner.dma.data_va;
        let status_va = inner.dma.status_va;

        let req_pa = inner.dma.req_pa;
        let data_pa = inner.dma.data_pa;
        let status_pa = inner.dma.status_pa;

        let old_avail_idx = inner.avail_idx;
        let ring_index = (old_avail_idx as usize) % VIRTIO_BLK_QUEUE_SIZE;
        let avail_ring = unsafe { inner.queue0.avail_ring_ptr(ring_index) };

        unsafe {
            // 1. request header —— 差异1:类型改成 OUT(写)
            let req = VirtioBlkReq {
                req_type: VIRTIO_BLK_T_OUT,
                reserved: 0,
                sector: sector as u64,
            };
            write_volatile(req_va as *mut VirtioBlkReq, req);

            write_volatile(status_va as *mut u8, 0xff);

            // 差异3:写之前,把要写的数据填进 DMA data buffer(给设备读)
            let dst = core::slice::from_raw_parts_mut(data_va as *mut u8, 512);
            dst.copy_from_slice(&in_buf[..512]);

            // 2. descriptor chain
            write_volatile(
                desc0,
                VirtqDesc {
                    addr: req_pa as u64,
                    len: core::mem::size_of::<VirtioBlkReq>() as u32,
                    flags: VIRTQ_DESC_F_NEXT,
                    next: 1,
                },
            );

            write_volatile(
                desc1,
                VirtqDesc {
                    addr: data_pa as u64,
                    len: 512,
                    // 差异2:写时数据缓冲是"设备读取",不设 F_WRITE
                    flags: VIRTQ_DESC_F_NEXT,
                    next: 2,
                },
            );

            write_volatile(
                desc2,
                VirtqDesc {
                    addr: status_pa as u64,
                    len: 1,
                    flags: VIRTQ_DESC_F_WRITE,   // status 仍是设备写,保留
                    next: 0,
                },
            );

            // 3. 放进 avail ring(与读相同)
            write_volatile(avail_flags, VIRTQ_AVAIL_F_NO_INTERRUPT);
            write_volatile(avail_ring, 0);

            core::sync::atomic::fence(core::sync::atomic::Ordering::SeqCst);

            let new_avail_idx = old_avail_idx.wrapping_add(1);
            inner.avail_idx = new_avail_idx;
            write_volatile(avail_idx_ptr, new_avail_idx);

            core::sync::atomic::fence(core::sync::atomic::Ordering::SeqCst);

            // 4. 通知设备
            self.header.notify_queue(0);

            // 5. 等 used ring(与读相同)
            let mut spin = 0usize;
            loop {
                let used_idx = read_volatile(used_idx_ptr);
                if used_idx != inner.last_used_idx {
                    break;
                }
                spin += 1;
                if spin > 10_000_000 {
                    log::error!(
                        "[virtio-blk] write sector {} timeout, avail_idx={}, used_idx={}, last_used_idx={}",
                        sector, inner.avail_idx, used_idx, inner.last_used_idx,
                    );
                    return -1;
                }
                core::hint::spin_loop();
            }

            // 6. 读 used elem
            let used_ring_index = (inner.last_used_idx as usize) % VIRTIO_BLK_QUEUE_SIZE;
            let used_ptr = inner.queue0.used_ring_ptr(used_ring_index);
            let used = read_volatile(used_ptr);
            inner.last_used_idx = inner.last_used_idx.wrapping_add(1);

            // 7. ack interrupt
            let isr = self.header.interrupt_status();
            if isr != 0 {
                self.header.ack_interrupt(isr);
            }

            // 8. 检查 status
            let status = read_volatile(status_va as *const u8);

            log::info!(
                "[virtio-blk] write done sector={}, used.id={}, used.len={}, status={}",
                sector, used.id, used.len, status,
            );

            if used.id != 0 {
                log::error!("[virtio-blk] unexpected used id: {}", used.id);
                return -1;
            }
            if status != VIRTIO_BLK_S_OK {
                log::error!("[virtio-blk] write sector {} failed, status={}", sector, status);
                return -1;
            }

            // 差异3:写不需要 copy out(没有数据要读回)
        }

        512
    }
}




use super::device::BlockDevice;
impl BlockDevice for VirtioBlkDevice {
    fn block_size(&self) -> usize {
        512
    }

    fn num_blocks(&self) -> usize {
        self.read_capacity() as usize 
    }

    fn read_block(&self, block_id: usize, buf: &mut [u8]) -> isize {
        if buf.len() != 512 {
            return -1;
        }

        self.read_sector_sync(block_id, buf)
    }

    fn write_block(&self, block_id: usize, buf: &[u8]) -> isize {
        if buf.len() != 512 {
            return -1;
        }
        self.write_sector_sync(block_id, buf)
    }

    
}

impl VirtioBlkDevice {
    // VirtioBlkDevice,读 device config 的 capacity
    fn read_capacity(&self) -> u64 {
        // mmio device config 在偏移 0x100
        let config_base = self.virt_base + 0x100;
        unsafe {
            let lo = read_volatile(config_base as *const u32) as u64;
            let hi = read_volatile((config_base + 4) as *const u32) as u64;
            (hi << 32) | lo
        }
    }
}


pub fn test_read_ext4_magic(dev: Arc<VirtioBlkDevice>) {
    let mut block = [0u8; 512];

    /*
     * ext4 magic 总偏移：
     *   1024 + 0x38 = 1080
     * 在 512B sector 下：
     *   sector = 2
     *   offset = 56
     */
    let ret = dev.read_block(2, &mut block);

    assert_eq!(ret, 512);

    let magic = u16::from_le_bytes([
        block[56],
        block[57],
    ]);

    log::info!(
        "[virtio-blk] ext4 magic from disk = {:#x}",
        magic,
    );

    assert_eq!(
        magic,
        0xef53,
        "[virtio-blk] bad ext4 magic"
    );
}

pub fn test_read_some_sectors(dev: alloc::sync::Arc<VirtioBlkDevice>) {
    for sector in 0..4 {
        let mut block = [0u8; 512];

        let ret = dev.read_block(sector, &mut block);

        log::info!(
            "[virtio-blk] sector {} ret={}, first16=[{:02x}, {:02x}, {:02x}, {:02x}, {:02x}, {:02x}, {:02x}, {:02x}, {:02x}, {:02x}, {:02x}, {:02x}, {:02x}, {:02x}, {:02x}, {:02x}]",
            sector,
            ret,
            block[0],
            block[1],
            block[2],
            block[3],
            block[4],
            block[5],
            block[6],
            block[7],
            block[8],
            block[9],
            block[10],
            block[11],
            block[12],
            block[13],
            block[14],
            block[15],
        );

        if sector == 2 {
            let magic = u16::from_le_bytes([block[56], block[57]]);
            log::info!(
                "[virtio-blk] sector 2 magic at offset 56 = {:#x}",
                magic,
            );
        }
    }
}



static mut VIRTIO_BLK_DEVICE: Option<Arc<VirtioBlkDevice>> = None;

pub fn init_global_from_phys_base(phys_base: usize) -> Option<Arc<VirtioBlkDevice>> {
    let dev = VirtioBlkDevice::init_from_phys_base(phys_base)?;

    unsafe {
        VIRTIO_BLK_DEVICE = Some(dev.clone());
    }

    Some(dev)
}

pub fn global_device() -> Option<Arc<VirtioBlkDevice>> {
    unsafe {
        VIRTIO_BLK_DEVICE.as_ref().cloned()
    }
}

pub fn global_block_device() -> Option<Arc<dyn BlockDevice>> {
    global_device().map(|dev| dev as Arc<dyn BlockDevice>)
}

