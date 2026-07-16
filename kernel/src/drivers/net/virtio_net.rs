 use alloc::vec::Vec;
use core::ptr::{read_volatile, write_volatile};

use crate::mm::{
    kernel_phys_to_virt,
    PAGE_SIZE,
    frame_allocator::{alloc_contiguous_frames},
};
use crate::drivers::virtio::queue::{
    VirtioQueue, VirtqDesc, VIRTQ_DESC_F_NEXT, VIRTQ_DESC_F_WRITE,
};
use crate::pci;
use crate::pci::probe::PciDeviceInfo;

pub const VIRTIO_NET_QUEUE_SIZE: usize = 8;

#[repr(C)]
pub struct VirtioNetHdr {
    pub flags: u8,
    pub gso_type: u8,
    pub hdr_len: u16,
    pub gso_size: u16,
    pub csum_start: u16,
    pub csum_offset: u16,
    pub num_buffers: u16,
}

pub struct VirtioNet {
    #[cfg(target_arch = "loongarch64")]
    pub common_va: usize,
    #[cfg(target_arch = "loongarch64")]
    pub notify_va: usize,
    #[cfg(target_arch = "loongarch64")]
    pub notify_off_multiplier: u32,
    #[cfg(target_arch = "loongarch64")]
    pub isr_va: usize,

    #[cfg(target_arch = "riscv64")]
    pub mmio_hdr: crate::drivers::virtio::transport::mmio::VirtioMmioHeader,

    pub rx: VirtioQueue,
    pub tx: VirtioQueue,
    pub mac: [u8; 6],
    pub rx_buffers: Vec<Vec<u8>>,
}

impl VirtioNet {
    pub fn init() -> Option<Self> {
        #[cfg(target_arch = "loongarch64")]
        {
            let info = crate::drivers::virtio::probe::find_virtio_net_pci()?;
            let addr = info.loc.addr();
            pci::enable_pci_device(addr);

            let regions = crate::drivers::virtio::transport::pci::parse_virtio_pci_caps(addr)?;
            let common = regions.common?;
            let notify = regions.notify?;
            let isr = regions.isr?;
            let device = regions.device;

            let common_va = common.va;
            let notify_va = notify.va;
            let isr_va = isr.va;
            let mul = regions.notify_off_multiplier;

            // Reset
            unsafe { write_volatile((common_va + 0x14) as *mut u8, 0) };
            while unsafe { read_volatile((common_va + 0x14) as *const u8) } != 0 {}

            // ACK + DRIVER
            unsafe { write_volatile((common_va + 0x14) as *mut u8, 1) };
            unsafe { write_volatile((common_va + 0x14) as *mut u8, 3) };

            // Features
            unsafe {
                write_volatile((common_va + 0x08) as *mut u32, 0);
                write_volatile((common_va + 0x0C) as *mut u32, 0);
                write_volatile((common_va + 0x14) as *mut u8, 11);
            }

            // Read MAC
            let mut mac = [0u8; 6];
            if let Some(d) = device {
                for i in 0..6 {
                    mac[i] = unsafe { read_volatile((d.va + i) as *const u8) };
                }
            }

            log::info!(
                "[virtio-net] MAC: {:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
            );

            let rx = Self::setup_queue(common_va, notify_va, mul, 0)?;
            let tx = Self::setup_queue(common_va, notify_va, mul, 1)?;

            // DRIVER_OK
            unsafe { write_volatile((common_va + 0x14) as *mut u8, 15) };

            Some(Self {
                common_va,
                notify_va,
                notify_off_multiplier: mul,
                isr_va,
                rx,
                tx,
                mac,
                rx_buffers: Vec::new(),
            })
        }

        #[cfg(target_arch = "riscv64")]
        {
            let pa = crate::drivers::virtio::transport::mmio::probe_virtio_net_mmio()?;
            let va = crate::mm::kernel_phys_to_virt(pa);
            let hdr = crate::drivers::virtio::transport::mmio::VirtioMmioHeader::new(va);

            // Reset
            hdr.reset();

            // ACK + DRIVER
            hdr.set_status_bits(crate::drivers::virtio::transport::mmio::VIRTIO_STATUS_ACKNOWLEDGE);
            hdr.set_status_bits(crate::drivers::virtio::transport::mmio::VIRTIO_STATUS_DRIVER);

            // Features
            hdr.write_driver_features(0, 0);
            hdr.set_status_bits(crate::drivers::virtio::transport::mmio::VIRTIO_STATUS_FEATURES_OK);

            // Read MAC (mmio device cfg at offset 0x100? 需要根据实际布局确认)
            // 这里简化：mmio 的 device cfg 通常在 queue 区域之后
            // 具体 offset 参考 virtio-mmio spec，一般是 0x100
            let mut mac = [0u8; 6];
            for i in 0..6 {
                mac[i] = unsafe { read_volatile((va + 0x100 + i) as *const u8) };
            }

            log::info!(
                "[virtio-net] MAC: {:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
            );

            let rx = Self::setup_queue_mmio(&hdr, 0)?;
            let tx = Self::setup_queue_mmio(&hdr, 1)?;

            hdr.set_status_bits(crate::drivers::virtio::transport::mmio::VIRTIO_STATUS_DRIVER_OK);

            Some(Self {
                mmio_hdr: hdr,
                rx,
                tx,
                mac,
                rx_buffers: Vec::new(),
            })
        }
    }

    #[cfg(target_arch = "loongarch64")]
    fn setup_queue(common_va: usize, notify_va: usize, mul: u32, qid: u16) -> Option<VirtioQueue> {
        let size = VIRTIO_NET_QUEUE_SIZE;
        unsafe {
            write_volatile((common_va + 0x16) as *mut u16, qid);
        }
        let qsize = unsafe { read_volatile((common_va + 0x18) as *const u16) };
        let size = if qsize == 0 {
            return None;
        } else if (qsize as usize) < size {
            qsize as usize
        } else {
            size
        };

        let vq = VirtioQueue::new_modern(size)?;

        unsafe {
            write_volatile((common_va + 0x20) as *mut u64, vq.desc_pa as u64);
            write_volatile((common_va + 0x28) as *mut u64, vq.avail_pa as u64);
            write_volatile((common_va + 0x30) as *mut u64, vq.used_pa as u64);
            write_volatile((common_va + 0x1C) as *mut u16, 1);
        }

        if qid == 0 {
            // RX: pre-fill buffers
            let mut buffers = Vec::with_capacity(size);
            for i in 0..size {
                let mut buf = Vec::with_capacity(2048);
                buf.resize(2048, 0);
                let paddr = crate::mm::virt_to_phys(buf.as_ptr() as usize);
                unsafe {
                    let d = &mut *vq.desc_mut(i);
                    d.addr = paddr as u64;
                    d.len = 2048;
                    d.flags = VIRTQ_DESC_F_WRITE;
                    d.next = 0;
                }
                let idx = unsafe { read_volatile(vq.avail_idx_ptr()) };
                unsafe { write_volatile(vq.avail_ring_ptr((idx as usize) % size), i as u16) };
                unsafe { write_volatile(vq.avail_idx_ptr(), idx + 1) };
                buffers.push(buf);
            }
            // kick
            let notify_off = unsafe { read_volatile((common_va + 0x1E) as *const u16) };
            let addr = notify_va + (notify_off as u32 * mul) as usize;
            unsafe { write_volatile(addr as *mut u16, qid) };
        }

        Some(vq)
    }

    #[cfg(target_arch = "riscv64")]
    fn setup_queue_mmio(hdr: &crate::drivers::virtio::transport::mmio::VirtioMmioHeader, qid: u32) -> Option<VirtioQueue> {
        let size = VIRTIO_NET_QUEUE_SIZE;
        hdr.select_queue(qid);
        let max = hdr.queue_size_max();
        let size = if max == 0 {
            return None;
        } else if (max as usize) < size {
            max as usize
        } else {
            size
        };

        let vq = VirtioQueue::new_modern(size)?;
        hdr.set_queue_size(size as u32);
        hdr.set_queue_desc_addr(vq.desc_pa);
        hdr.set_queue_driver_addr(vq.avail_pa);
        hdr.set_queue_device_addr(vq.used_pa);
        hdr.set_queue_ready(1);

        if qid == 0 {
            for i in 0..size {
                let mut buf = Vec::with_capacity(2048);
                buf.resize(2048, 0);
                let paddr = crate::mm::virt_to_phys(buf.as_ptr() as usize);
                unsafe {
                    let d = &mut *vq.desc_mut(i);
                    d.addr = paddr as u64;
                    d.len = 2048;
                    d.flags = VIRTQ_DESC_F_WRITE;
                    d.next = 0;
                }
                let idx = unsafe { read_volatile(vq.avail_idx_ptr()) };
                unsafe { write_volatile(vq.avail_ring_ptr((idx as usize) % size), i as u16) };
                unsafe { write_volatile(vq.avail_idx_ptr(), idx + 1) };
            }
            hdr.notify_queue(qid);
        }

        Some(vq)
    }

    pub fn send(&mut self, packet: &[u8]) {
        if packet.len() > 1514 {
            return;
        }

        let tx = &self.tx;
        let head = 0usize; // 简化：假设描述符 0 和 1 可用
        let id1 = 0;
        let id2 = 1;

        static mut TX_HDR: VirtioNetHdr = VirtioNetHdr {
            flags: 0, gso_type: 0, hdr_len: 0,
            gso_size: 0, csum_start: 0, csum_offset: 0, num_buffers: 0,
        };
        let hdr_v = unsafe { &mut TX_HDR as *mut VirtioNetHdr as usize };
        let hdr_p = crate::mm::virt_to_phys(hdr_v);

        unsafe {
            let d1 = &mut *tx.desc_mut(id1);
            d1.addr = hdr_p as u64;
            d1.len = core::mem::size_of::<VirtioNetHdr>() as u32;
            d1.flags = VIRTQ_DESC_F_NEXT;
            d1.next = id2 as u16;

            let d2 = &mut *tx.desc_mut(id2);
            d2.addr = crate::mm::virt_to_phys(packet.as_ptr() as usize) as u64;
            d2.len = packet.len() as u32;
            d2.flags = 0;
            d2.next = 0;
        }

        let idx = unsafe { read_volatile(tx.avail_idx_ptr()) };
        unsafe { write_volatile(tx.avail_ring_ptr((idx as usize) % tx.size), id1 as u16) };
        unsafe { write_volatile(tx.avail_idx_ptr(), idx + 1) };

        // kick
        #[cfg(target_arch = "loongarch64")]
        {
            let notify_off = unsafe { read_volatile((self.common_va + 0x1E) as *const u16) };
            let addr = self.notify_va + (notify_off as u32 * self.notify_off_multiplier) as usize;
            unsafe { write_volatile(addr as *mut u16, 1) };
        }
        #[cfg(target_arch = "riscv64")]
        {
            self.mmio_hdr.notify_queue(1);
        }
    }

    pub fn poll_rx(&mut self, out: &mut [u8]) -> usize {
        let rx = &self.rx;
        let used_idx = unsafe { read_volatile(rx.used_idx_ptr()) };
        if rx.last_used == used_idx {
            return 0;
        }

        let elem = unsafe { &*rx.used_ring_ptr((rx.last_used as usize) % rx.size) };
        let id = elem.id as usize;
        let len = elem.len as usize;

        // 拷贝数据
        let copy_len = if len > out.len() { out.len() } else { len };
        if id < self.rx_buffers.len() {
            out[..copy_len].copy_from_slice(&self.rx_buffers[id][..copy_len]);
        } else {
            // 直接从 desc 的 addr 读（如果 rx_buffers 没维护）
            unsafe {
                let d = &*rx.desc_mut(id);
                let src = crate::mm::kernel_phys_to_virt(d.addr as usize) as *const u8;
                core::ptr::copy_nonoverlapping(src, out.as_mut_ptr(), copy_len);
            }
        }

        // 回收
        if id < self.rx_buffers.len() {
            let paddr = crate::mm::virt_to_phys(self.rx_buffers[id].as_ptr() as usize);
            unsafe {
                let d = &mut *rx.desc_mut(id);
                d.addr = paddr as u64;
                d.len = 2048;
                d.flags = VIRTQ_DESC_F_WRITE;
                d.next = 0;
            }
        }
        let idx = unsafe { read_volatile(rx.avail_idx_ptr()) };
        unsafe { write_volatile(rx.avail_ring_ptr((idx as usize) % rx.size), id as u16) };
        unsafe { write_volatile(rx.avail_idx_ptr(), idx + 1) };

        // kick
        #[cfg(target_arch = "loongarch64")]
        {
            let notify_off = unsafe { read_volatile((self.common_va + 0x1E) as *const u16) };
            let addr = self.notify_va + (notify_off as u32 * self.notify_off_multiplier) as usize;
            unsafe { write_volatile(addr as *mut u16, 0) };
        }
        #[cfg(target_arch = "riscv64")]
        {
            self.mmio_hdr.notify_queue(0);
        }

        unsafe {
            // 这里需要改 VirtioQueue 的 last_used，但它是 const 引用
            // 建议把 last_used 放到 VirtioNet 里维护，或者让 VirtioQueue 的 last_used 是 Cell/Atomic
        }

        copy_len
    }
}