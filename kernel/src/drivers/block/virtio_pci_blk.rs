use alloc::sync::Arc;
use core::ptr::{read_volatile, write_volatile};

use crate::sync::spin::Mutex;

use super::device::BlockDevice;
use crate::drivers::virtio::transport::pci::{
    VirtioPciRegions,
    VirtioPciRegion,
};
use crate::drivers::virtio::queue::{
    VirtioQueue,
    VirtqDesc,
    VIRTIO_BLK_QUEUE_SIZE,
    VIRTQ_DESC_F_NEXT,
    VIRTQ_DESC_F_WRITE,
    VIRTQ_AVAIL_F_NO_INTERRUPT,
    VIRTIO_BLK_T_OUT,
    VIRTIO_BLK_T_IN,
    VIRTIO_BLK_S_OK,
};

use super::virtio_blk_dma::{
    VirtioBlkDma,
    VirtioBlkReq,
};



const COMMON_DEVICE_FEATURE_SELECT: usize = 0x00;
const COMMON_DEVICE_FEATURE: usize = 0x04;
const COMMON_DRIVER_FEATURE_SELECT: usize = 0x08;
const COMMON_DRIVER_FEATURE: usize = 0x0c;
const COMMON_MSIX_CONFIG: usize = 0x10;
const COMMON_NUM_QUEUES: usize = 0x12;
const COMMON_DEVICE_STATUS: usize = 0x14;
const COMMON_CONFIG_GENERATION: usize = 0x15;

const COMMON_QUEUE_SELECT: usize = 0x16;
const COMMON_QUEUE_SIZE: usize = 0x18;
const COMMON_QUEUE_MSIX_VECTOR: usize = 0x1a;
const COMMON_QUEUE_ENABLE: usize = 0x1c;
const COMMON_QUEUE_NOTIFY_OFF: usize = 0x1e;
const COMMON_QUEUE_DESC: usize = 0x20;
const COMMON_QUEUE_DRIVER: usize = 0x28;
const COMMON_QUEUE_DEVICE: usize = 0x30;

const VIRTIO_STATUS_ACKNOWLEDGE: u8 = 1;
const VIRTIO_STATUS_DRIVER: u8 = 2;
const VIRTIO_STATUS_DRIVER_OK: u8 = 4;
const VIRTIO_STATUS_FEATURES_OK: u8 = 8;
const VIRTIO_STATUS_FAILED: u8 = 128;

const VIRTIO_F_VERSION_1: u64 = 1u64 << 32;


#[derive(Clone, Copy)]
pub struct VirtioPciTransport {
    common: VirtioPciRegion,
    notify: VirtioPciRegion,
    isr: VirtioPciRegion,
    device: Option<VirtioPciRegion>,
    notify_off_multiplier: u32,
}

impl VirtioPciTransport {
    pub fn new(regions: VirtioPciRegions) -> Option<Self> {
        Some(Self {
            common: regions.common?,
            notify: regions.notify?,
            isr: regions.isr?,
            device: regions.device,
            notify_off_multiplier: regions.notify_off_multiplier,
        })
    }

    fn common_read_u8(&self, off: usize) -> u8 {
        unsafe {
            read_volatile((self.common.va + off) as *const u8)
        }
    }

    fn common_write_u8(&self, off: usize, val: u8) {
        unsafe {
            write_volatile((self.common.va + off) as *mut u8, val);
        }
    }

    fn common_read_u16(&self, off: usize) -> u16 {
        unsafe {
            u16::from_le(read_volatile((self.common.va + off) as *const u16))
        }
    }

    fn common_write_u16(&self, off: usize, val: u16) {
        unsafe {
            write_volatile((self.common.va + off) as *mut u16, val.to_le());
        }
    }

    fn common_read_u32(&self, off: usize) -> u32 {
        unsafe {
            u32::from_le(read_volatile((self.common.va + off) as *const u32))
        }
    }

    fn common_write_u32(&self, off: usize, val: u32) {
        unsafe {
            write_volatile((self.common.va + off) as *mut u32, val.to_le());
        }
    }

    fn common_write_u64(&self, off: usize, val: u64) {
        unsafe {
            write_volatile((self.common.va + off) as *mut u64, val.to_le());
        }
    }

    fn status(&self) -> u8 {
        self.common_read_u8(COMMON_DEVICE_STATUS)
    }

    fn set_status(&self, status: u8) {
        self.common_write_u8(COMMON_DEVICE_STATUS, status);
        core::sync::atomic::fence(core::sync::atomic::Ordering::SeqCst);
    }

    fn add_status(&self, bits: u8) {
        let old = self.status();
        self.set_status(old | bits);
    }

    fn reset(&self) {
        self.set_status(0);

        /*
         * 等设备真的归零。
         */
        for _ in 0..100000 {
            if self.status() == 0 {
                return;
            }
            core::hint::spin_loop();
        }

        log::warn!("[virtio-pci] reset timeout, status={:#x}", self.status());
    }

    fn fail(&self) {
        self.add_status(VIRTIO_STATUS_FAILED);
    }

    fn read_device_features(&self, select: u32) -> u32 {
        self.common_write_u32(COMMON_DEVICE_FEATURE_SELECT, select);
        core::sync::atomic::fence(core::sync::atomic::Ordering::SeqCst);
        self.common_read_u32(COMMON_DEVICE_FEATURE)
    }

    fn write_driver_features(&self, select: u32, features: u32) {
        self.common_write_u32(COMMON_DRIVER_FEATURE_SELECT, select);
        core::sync::atomic::fence(core::sync::atomic::Ordering::SeqCst);
        self.common_write_u32(COMMON_DRIVER_FEATURE, features);
        core::sync::atomic::fence(core::sync::atomic::Ordering::SeqCst);
    }

    fn select_queue(&self, queue: u16) {
        self.common_write_u16(COMMON_QUEUE_SELECT, queue);
        core::sync::atomic::fence(core::sync::atomic::Ordering::SeqCst);
    }

    fn queue_size(&self) -> u16 {
        self.common_read_u16(COMMON_QUEUE_SIZE)
    }

    fn set_queue_size(&self, size: u16) {
        self.common_write_u16(COMMON_QUEUE_SIZE, size);
    }

    fn queue_enable(&self) -> u16 {
        self.common_read_u16(COMMON_QUEUE_ENABLE)
    }

    fn set_queue_enable(&self, enable: u16) {
        self.common_write_u16(COMMON_QUEUE_ENABLE, enable);
        core::sync::atomic::fence(core::sync::atomic::Ordering::SeqCst);
    }

    fn queue_notify_off(&self) -> u16 {
        self.common_read_u16(COMMON_QUEUE_NOTIFY_OFF)
    }

    fn set_queue_desc(&self, pa: usize) {
        self.common_write_u64(COMMON_QUEUE_DESC, pa as u64);
    }

    fn set_queue_driver(&self, pa: usize) {
        self.common_write_u64(COMMON_QUEUE_DRIVER, pa as u64);
    }

    fn set_queue_device(&self, pa: usize) {
        self.common_write_u64(COMMON_QUEUE_DEVICE, pa as u64);
    }

    fn notify_queue(&self, queue: u16, queue_notify_off: u16) {
        let notify_addr = self.notify.va
            + (queue_notify_off as usize) * (self.notify_off_multiplier as usize);

        unsafe {
            write_volatile(notify_addr as *mut u16, queue.to_le());
        }

        core::sync::atomic::fence(core::sync::atomic::Ordering::SeqCst);
    }

    fn ack_isr(&self) -> u8 {
        unsafe {
            read_volatile(self.isr.va as *const u8)
        }
    }
}

pub struct VirtioPciBlkInner {
    queue0: VirtioQueue,
    dma: VirtioBlkDma,
    avail_idx: u16,
    last_used_idx: u16,
    queue_notify_off: u16,
}

pub struct VirtioPciBlkDevice {
    transport: VirtioPciTransport,
    inner: Mutex<VirtioPciBlkInner>,
}

impl VirtioPciBlkDevice {
    pub fn init(regions: VirtioPciRegions) -> Option<Arc<Self>> {
        let transport = VirtioPciTransport::new(regions)?;

        log::info!(
            "[virtio-pci-blk] init: common_va={:#x}, notify_va={:#x}, isr_va={:#x}, notify_mul={}",
            transport.common.va,
            transport.notify.va,
            transport.isr.va,
            transport.notify_off_multiplier,
        );

        transport.reset();

        transport.add_status(VIRTIO_STATUS_ACKNOWLEDGE);
        transport.add_status(VIRTIO_STATUS_DRIVER);

        let features0 = transport.read_device_features(0);
        let features1 = transport.read_device_features(1);

        log::info!(
            "[virtio-pci-blk] features word0={:#x}, word1={:#x}",
            features0,
            features1,
        );

        let version1_word = ((VIRTIO_F_VERSION_1 >> 32) & 0xffff_ffff) as u32;

        if (features1 & version1_word) == 0 {
            log::error!("[virtio-pci-blk] device lacks VIRTIO_F_VERSION_1");
            transport.fail();
            return None;
        }

        /*
         * 第一版只接受 VERSION_1，不启用其他 feature。
         */
        transport.write_driver_features(0, 0);
        transport.write_driver_features(1, version1_word);

        transport.add_status(VIRTIO_STATUS_FEATURES_OK);

        if (transport.status() & VIRTIO_STATUS_FEATURES_OK) == 0 {
            log::error!("[virtio-pci-blk] FEATURES_OK rejected");
            transport.fail();
            return None;
        }

        transport.select_queue(0);

        let max_queue_size = transport.queue_size();

        if max_queue_size == 0 {
            log::error!("[virtio-pci-blk] queue0 not available");
            transport.fail();
            return None;
        }

        if max_queue_size < VIRTIO_BLK_QUEUE_SIZE as u16 {
            log::error!(
                "[virtio-pci-blk] queue0 too small: max={}, need={}",
                max_queue_size,
                VIRTIO_BLK_QUEUE_SIZE,
            );
            transport.fail();
            return None;
        }

        if transport.queue_enable() != 0 {
            log::error!("[virtio-pci-blk] queue0 already enabled");
            transport.fail();
            return None;
        }

        let queue0 = match VirtioQueue::new_modern(VIRTIO_BLK_QUEUE_SIZE) {
            Some(q) => q,
            None => {
                log::error!("[virtio-pci-blk] alloc queue failed");
                transport.fail();
                return None;
            }
        };

        transport.set_queue_size(VIRTIO_BLK_QUEUE_SIZE as u16);
        transport.set_queue_desc(queue0.desc_pa);
        transport.set_queue_driver(queue0.avail_pa);
        transport.set_queue_device(queue0.used_pa);

        let queue_notify_off = transport.queue_notify_off();

        transport.set_queue_enable(1);

        log::info!(
            "[virtio-pci-blk] queue0 ready: desc={:#x}, avail={:#x}, used={:#x}, notify_off={}",
            queue0.desc_pa,
            queue0.avail_pa,
            queue0.used_pa,
            queue_notify_off,
        );

        let dma = match VirtioBlkDma::new() {
            Some(dma) => dma,
            None => {
                log::error!("[virtio-pci-blk] alloc dma failed");
                transport.fail();
                return None;
            }
        };

        transport.add_status(VIRTIO_STATUS_DRIVER_OK);
        log::info!("[pci-blk] queue pa={:#x} pages={}", queue0.queue_pa, queue0.pages);
        log::info!(
            "[virtio-pci-blk] init done, status={:#x}",
            transport.status(),
        );
        
        Some(Arc::new(Self {
            transport,
            inner: Mutex::new(VirtioPciBlkInner {
                queue0,
                dma,
                avail_idx: 0,
                last_used_idx: 0,
                queue_notify_off,
            }),
        }))
    }
}

impl VirtioPciBlkDevice {
    fn read_sector_sync(&self, sector: usize, out: &mut [u8]) -> isize {
        if out.len() != 512 {
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

        let queue_notify_off = inner.queue_notify_off;

        unsafe {
            let req = VirtioBlkReq {
                req_type: VIRTIO_BLK_T_IN,
                reserved: 0,
                sector: sector as u64,
            };

            write_volatile(req_va as *mut VirtioBlkReq, req);
            write_volatile(status_va as *mut u8, 0xff);
            core::ptr::write_bytes(data_va as *mut u8, 0, 512);

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

            write_volatile(avail_flags, VIRTQ_AVAIL_F_NO_INTERRUPT);
            write_volatile(avail_ring, 0);

            core::sync::atomic::fence(core::sync::atomic::Ordering::SeqCst);

            let new_avail_idx = old_avail_idx.wrapping_add(1);
            inner.avail_idx = new_avail_idx;

            write_volatile(avail_idx_ptr, new_avail_idx);

            core::sync::atomic::fence(core::sync::atomic::Ordering::SeqCst);

            self.transport.notify_queue(0, queue_notify_off);

            let mut spin = 0usize;

            loop {
                let used_idx = read_volatile(used_idx_ptr);

                if used_idx != inner.last_used_idx {
                    break;
                }

                spin += 1;

                if spin > 10_000_000 {
                    log::error!(
                        "[virtio-pci-blk] read sector {} timeout, avail_idx={}, used_idx={}, last_used_idx={}",
                        sector,
                        inner.avail_idx,
                        used_idx,
                        inner.last_used_idx,
                    );
                    return -1;
                }

                core::hint::spin_loop();
            }

            let used_ring_index =
                (inner.last_used_idx as usize) % VIRTIO_BLK_QUEUE_SIZE;

            let used_ptr = inner.queue0.used_ring_ptr(used_ring_index);
            let used = read_volatile(used_ptr);

            inner.last_used_idx = inner.last_used_idx.wrapping_add(1);

            let _isr = self.transport.ack_isr();

            let status = read_volatile(status_va as *const u8);

            // log::info!(
            //     "[virtio-pci-blk] done sector={}, used.id={}, used.len={}, status={}",
            //     sector,
            //     used.id,
            //     used.len,
            //     status,
            // );

            if used.id != 0 {
                log::error!("[virtio-pci-blk] unexpected used id={}", used.id);
                return -1;
            }

            if status != VIRTIO_BLK_S_OK {
                log::error!(
                    "[virtio-pci-blk] read sector {} failed, status={}",
                    sector,
                    status,
                );
                return -1;
            }

            let data = core::slice::from_raw_parts(data_va as *const u8, 512);
            out.copy_from_slice(data);
        }

        512
    }
}


impl VirtioPciBlkDevice {
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

        let queue_notify_off = inner.queue_notify_off;

        unsafe {

            let req = VirtioBlkReq {
                req_type: VIRTIO_BLK_T_OUT,
                reserved: 0,
                sector: sector as u64,
            };

            write_volatile(req_va as *mut VirtioBlkReq, req);
            write_volatile(status_va as *mut u8, 0xff);


            let dst = core::slice::from_raw_parts_mut(data_va as *mut u8, 512);
            dst.copy_from_slice(&in_buf[..512]);

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
                    
                    flags: VIRTQ_DESC_F_NEXT,
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

            write_volatile(avail_flags, VIRTQ_AVAIL_F_NO_INTERRUPT);
            write_volatile(avail_ring, 0);

            core::sync::atomic::fence(core::sync::atomic::Ordering::SeqCst);

            let new_avail_idx = old_avail_idx.wrapping_add(1);
            inner.avail_idx = new_avail_idx;
            write_volatile(avail_idx_ptr, new_avail_idx);

            core::sync::atomic::fence(core::sync::atomic::Ordering::SeqCst);

            self.transport.notify_queue(0, queue_notify_off);

            let mut spin = 0usize;
            loop {
                let used_idx = read_volatile(used_idx_ptr);
                if used_idx != inner.last_used_idx {
                    break;
                }
                spin += 1;
                if spin > 10_000_000 {
                    log::error!(
                        "[virtio-pci-blk] write sector {} timeout, avail_idx={}, used_idx={}, last_used_idx={}",
                        sector, inner.avail_idx, used_idx, inner.last_used_idx,
                    );
                    return -1;
                }
                core::hint::spin_loop();
            }

            let used_ring_index = (inner.last_used_idx as usize) % VIRTIO_BLK_QUEUE_SIZE;
            let used_ptr = inner.queue0.used_ring_ptr(used_ring_index);
            let used = read_volatile(used_ptr);
            inner.last_used_idx = inner.last_used_idx.wrapping_add(1);

            let _isr = self.transport.ack_isr();

            let status = read_volatile(status_va as *const u8);

            log::info!(
                "[virtio-pci-blk] write done sector={}, used.id={}, used.len={}, status={}",
                sector, used.id, used.len, status,
            );

            if used.id != 0 {
                log::error!("[virtio-pci-blk] unexpected used id={}", used.id);
                return -1;
            }
            if status != VIRTIO_BLK_S_OK {
                log::error!("[virtio-pci-blk] write sector {} failed, status={}", sector, status);
                return -1;
            }

        }

        512
    }
}

impl BlockDevice for VirtioPciBlkDevice {
    fn block_size(&self) -> usize {
        512
    }

    fn num_blocks(&self) -> usize {
        self.read_capacity() as usize
    }

    fn read_block(&self, block_id: usize, buf: &mut [u8]) -> isize {
        self.read_sector_sync(block_id, buf)
    }
    fn write_block(&self, block_id: usize, buf: &[u8]) -> isize {
        if buf.len() != 512 {
            return -1;
        }
        self.write_sector_sync(block_id, buf)
    }
}
impl VirtioPciBlkDevice {
    fn read_capacity(&self) -> u64 {
        if let Some(dev_region) = self.transport.device {
            unsafe {
                let lo = read_volatile(dev_region.va as *const u32) as u64;
                let hi = read_volatile((dev_region.va + 4) as *const u32) as u64;
                (hi << 32) | lo
            }
        } else {
            0
        }
    }

}

pub fn test_read_ext4_magic(dev: Arc<VirtioPciBlkDevice>) {
    let mut block = [0u8; 512];

    let ret = dev.read_block(2, &mut block);
    assert_eq!(ret, 512);

    let magic = u16::from_le_bytes([block[56], block[57]]);

    log::info!(
        "[virtio-pci-blk] ext4 magic from disk = {:#x}",
        magic,
    );

    assert_eq!(magic, 0xef53, "[virtio-pci-blk] bad ext4 magic");
}