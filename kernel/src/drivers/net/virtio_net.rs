use alloc::vec::Vec;
use core::ptr::{read_volatile, write_volatile};

use crate::mm::virt_to_phys;
use crate::drivers::virtio::queue::{VirtioQueue, VIRTQ_DESC_F_NEXT, VIRTQ_DESC_F_WRITE};

pub const NET_QUEUE_SIZE: usize = 8;

// virtio-pci common configuration 寄存器偏移（RISC-V / LoongArch 通用）
mod pci_regs {
    pub const DEVICE_STATUS:    usize = 0x14;
    pub const QUEUE_SELECT:     usize = 0x16;
    pub const QUEUE_SIZE_REG:   usize = 0x18;
    pub const QUEUE_ENABLE:     usize = 0x1C;
    pub const QUEUE_NOTIFY_OFF: usize = 0x1E;
    pub const QUEUE_DESC_LO:    usize = 0x20;
    pub const QUEUE_AVAIL_LO:   usize = 0x28;
    pub const QUEUE_USED_LO:    usize = 0x30;
}

/// 协商了 VIRTIO_NET_F_MRG_RXBUF，线上 hdr 固定 12 字节
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
    rx: VirtioQueue,
    tx: VirtioQueue,
    pub mac: [u8; 6],
    rx_bufs: Vec<Vec<u8>>,
    rx_last_used: u16,

    common_va: usize,
    notify_va: usize,
    notify_mul: u32,
    rx_notify_off: u16,
    tx_notify_off: u16,
}

#[inline]
fn dev_fence() {
    core::sync::atomic::fence(core::sync::atomic::Ordering::SeqCst);
}

impl VirtioNet {
    /// 双架构统一入口：都走 PCI
    pub fn init() -> Option<Self> {
        Self::init_pci()
    }

    fn init_pci() -> Option<Self> {
        use crate::pci::ecam::enable_pci_device;
        use crate::drivers::virtio::transport::pci::parse_virtio_pci_caps;
        use pci_regs::*;

        log::info!("[virtio-net] PCI: scanning for device...");
        let loc = Self::find_pci()?;
        let addr = loc.addr();

        crate::pci::bar::assign_all_bars(addr);   // 先分 BAR
        enable_pci_device(addr);                  // 再开 Memory Space + Bus Master

        let caps = parse_virtio_pci_caps(addr)?;
        log::info!("[virtio-net] PCI: caps parsed ok");

        let common = caps.common.as_ref()?;
        let notify = caps.notify.as_ref()?;
        let device = caps.device.as_ref()?;
        log::info!("[virtio-net] PCI: common={:#x} notify={:#x} device={:#x}",
            common.va, notify.va, device.va);

        let common_va = common.va;
        let notify_va = notify.va;
        let notify_mul = caps.notify_off_multiplier;

        // ---- Reset ----
        unsafe { write_volatile((common_va + DEVICE_STATUS) as *mut u8, 0) };
        dev_fence();
        while unsafe { read_volatile((common_va + DEVICE_STATUS) as *const u8) } != 0 {
            dev_fence();
        }

        // ---- ACK -> DRIVER ----
        unsafe { write_volatile((common_va + DEVICE_STATUS) as *mut u8, 1) };
        dev_fence();
        unsafe { write_volatile((common_va + DEVICE_STATUS) as *mut u8, 3) };
        dev_fence();

        // ---- features：MRG_RXBUF (bit 15) + VERSION_1 (bit 32) ----
        unsafe {
            write_volatile((common_va + 0x08) as *mut u32, 0);        // driver_feature_select = 0
            dev_fence();
            write_volatile((common_va + 0x0C) as *mut u32, 1 << 15);  // MRG_RXBUF
            dev_fence();
            write_volatile((common_va + 0x08) as *mut u32, 1);        // select = 1
            dev_fence();
            write_volatile((common_va + 0x0C) as *mut u32, 1);        // VERSION_1
            dev_fence();
        }

        // ---- FEATURES_OK ----
        unsafe { write_volatile((common_va + DEVICE_STATUS) as *mut u8, 11) };
        dev_fence();
        let mut retries = 1000;
        loop {
            let s = unsafe { read_volatile((common_va + DEVICE_STATUS) as *const u8) };
            if s & 8 != 0 { break; }
            retries -= 1;
            if retries == 0 {
                log::warn!("[virtio-net] PCI: FEATURES_OK timeout, status={:#x}", s);
                return None;
            }
            dev_fence();
        }

        // ---- MAC ----
        let mut mac = [0u8; 6];
        for i in 0..6 {
            dev_fence();
            mac[i] = unsafe { read_volatile((device.va + i) as *const u8) };
        }
        if mac == [0u8; 6] {
            mac = [0x52, 0x54, 0x00, 0x12, 0x34, 0x56];
            log::warn!("[virtio-net] PCI: device MAC is zero, using default");
        }
        log::info!("[virtio-net] PCI: MAC={:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

        // ---- 队列 ----
        let (rx, rx_notify_off) = Self::setup_queue_pci(common_va, 0)?;
        let (tx, tx_notify_off) = Self::setup_queue_pci(common_va, 1)?;

        unsafe { write_volatile((common_va + DEVICE_STATUS) as *mut u8, 15) }; // DRIVER_OK
        dev_fence();
        log::info!("[virtio-net] PCI: DRIVER_OK");

        // ---- RX pre-fill ----
        let mut rx_bufs: Vec<Vec<u8>> = Vec::new();
        for i in 0..rx.size {
            let buf = alloc::vec![0u8; 2048];
            let paddr = virt_to_phys(buf.as_ptr() as usize);
            unsafe {
                let d = &mut *rx.desc_mut(i);
                d.addr = paddr as u64;
                d.len = 2048;
                d.flags = VIRTQ_DESC_F_WRITE;
                d.next = 0;

                let idx = read_volatile(rx.avail_idx_ptr());
                write_volatile(rx.avail_ring_ptr((idx as usize) % rx.size), i as u16);
            }
            dev_fence();
            unsafe {
                let idx = read_volatile(rx.avail_idx_ptr());
                write_volatile(rx.avail_idx_ptr(), idx.wrapping_add(1));
            }
            dev_fence();
            rx_bufs.push(buf);
        }
        {
            let addr = notify_va + (rx_notify_off as u32 * notify_mul) as usize;
            unsafe { write_volatile(addr as *mut u16, 0) };
            dev_fence();
        }
        log::info!("[virtio-net] PCI: initialized, rxq pa={:#x} txq pa={:#x}",
            rx.queue_pa, tx.queue_pa);

        Some(Self {
            common_va, notify_va, notify_mul,
            rx, tx, mac, rx_bufs,
            rx_last_used: 0,
            rx_notify_off, tx_notify_off,
        })
    }

    fn find_pci() -> Option<crate::pci::probe::PciDeviceLocation> {
        use crate::pci::probe::{read_device_info, PciDeviceLocation};
        for bus in 0u8..=0 {
            for device in 0u8..32 {
                for function in 0u8..8 {
                    let loc = PciDeviceLocation { bus, device, function };
                    let Some(info) = read_device_info(loc) else {
                        if function == 0 { break; }
                        continue;
                    };
                    if info.vendor_id == 0x1AF4
                        && (info.device_id == 0x1000 || info.device_id == 0x1041)
                    {
                        log::info!("[virtio-net] find_pci: hit 1AF4:{:04x} at bus={} dev={} fn={}",
                            info.device_id, bus, device, function);
                        return Some(loc);
                    }
                    if function == 0 && (info.header_type & 0x80) == 0 {
                        break;
                    }
                }
            }
        }
        log::warn!("[virtio-net] find_pci: no virtio-net (1AF4:1000/1041) found");
        None
    }

    fn setup_queue_pci(common_va: usize, qid: u16) -> Option<(VirtioQueue, u16)> {
        use pci_regs::*;
        unsafe { write_volatile((common_va + QUEUE_SELECT) as *mut u16, qid) };
        dev_fence();
        let max = unsafe { read_volatile((common_va + QUEUE_SIZE_REG) as *const u16) } as usize;
        dev_fence();
        log::info!("[virtio-net] setup_queue qid={} queue_size_max={}", qid, max);
        if max == 0 {
            // modern 设备不会为 0；为 0 说明寄存器映射/地址有问题，直接失败比硬撑好调
            log::warn!("[virtio-net] setup_queue qid={}: max=0, bail", qid);
            return None;
        }
        let size = max.min(NET_QUEUE_SIZE);

        let vq = VirtioQueue::new_modern(size)?;
        unsafe {
            write_volatile((common_va + QUEUE_SIZE_REG) as *mut u16, size as u16);
            dev_fence();
            // common cfg max_access_size=4，u64 拆成两个 u32 写
            let mut wr64 = |off: usize, val: u64| {
                write_volatile((common_va + off) as *mut u32, val as u32);
                write_volatile((common_va + off + 4) as *mut u32, (val >> 32) as u32);
            };
            wr64(QUEUE_DESC_LO, vq.desc_pa as u64);
            wr64(QUEUE_AVAIL_LO, vq.avail_pa as u64);
            wr64(QUEUE_USED_LO, vq.used_pa as u64);
            dev_fence();
            write_volatile((common_va + QUEUE_ENABLE) as *mut u16, 1);
        }
        dev_fence();
        let mut notify_off = unsafe { read_volatile((common_va + QUEUE_NOTIFY_OFF) as *const u16) };
        dev_fence();
        if notify_off == 0 && qid > 0 {
            log::warn!("[virtio-net] setup_queue qid={}: notify_off=0, fallback to {}", qid, qid);
            notify_off = qid;
        }
        log::info!("[virtio-net] queue{} ready: size={} notify_off={}", qid, size, notify_off);
        Some((vq, notify_off))
    }
    
    pub fn send(&mut self, packet: &[u8]) {
        if packet.len() > 1514 { return; }

        let tx = &self.tx;
        let id1 = 0usize;
        let id2 = 1usize;

        let tx_hdr = VirtioNetHdr {
            flags: 0, gso_type: 0, hdr_len: 0,
            gso_size: 0, csum_start: 0, csum_offset: 0, num_buffers: 0,
        };
        let hdr_p = virt_to_phys(&tx_hdr as *const _ as usize);

        unsafe {
            let d1 = &mut *tx.desc_mut(id1);
            d1.addr = hdr_p as u64;
            d1.len = core::mem::size_of::<VirtioNetHdr>() as u32; // = 12
            d1.flags = VIRTQ_DESC_F_NEXT;
            d1.next = id2 as u16;

            let d2 = &mut *tx.desc_mut(id2);
            d2.addr = virt_to_phys(packet.as_ptr() as usize) as u64;
            d2.len = packet.len() as u32;
            d2.flags = 0;
            d2.next = 0;
        }

        dev_fence();
        let used_before = unsafe { read_volatile(tx.used_idx_ptr()) };
        dev_fence();

        let idx = unsafe { read_volatile(tx.avail_idx_ptr()) };
        unsafe { write_volatile(tx.avail_ring_ptr((idx as usize) % tx.size), id1 as u16) };
        dev_fence();
        unsafe { write_volatile(tx.avail_idx_ptr(), idx.wrapping_add(1)) };
        dev_fence();

        // kick TX：notify 值 = queue index
        let addr = self.notify_va + (self.tx_notify_off as u32 * self.notify_mul) as usize;
        unsafe { write_volatile(addr as *mut u16, 1) };
        dev_fence();

        let mut spin = 0usize;
        loop {
            dev_fence();
            let used_now = unsafe { read_volatile(tx.used_idx_ptr()) };
            if used_now != used_before { break; }
            spin += 1;
            if spin > 1_000_000 {
                log::warn!("[virtio-net] TX timeout: used_before={} used_now={}", used_before, used_now);
                break;
            }
            core::hint::spin_loop();
        }
    }

    pub fn poll_rx(&mut self, out: &mut [u8]) -> usize {
        let rx = &self.rx;
        dev_fence();
        let used_idx = unsafe { read_volatile(rx.used_idx_ptr()) };
        if self.rx_last_used == used_idx { return 0; }

        let elem = unsafe { &*rx.used_ring_ptr((self.rx_last_used as usize) % rx.size) };
        let id = elem.id as usize;
        let len = elem.len as usize;

        let copy_len = if len > out.len() { out.len() } else { len };
        unsafe {
            let d = &*rx.desc_mut(id);
            let src = crate::mm::kernel_phys_to_virt(d.addr as usize) as *const u8;
            core::ptr::copy_nonoverlapping(src, out.as_mut_ptr(), copy_len);
        }

        // 回收 buffer
        let idx = unsafe { read_volatile(rx.avail_idx_ptr()) };
        unsafe { write_volatile(rx.avail_ring_ptr((idx as usize) % rx.size), id as u16) };
        dev_fence();
        unsafe { write_volatile(rx.avail_idx_ptr(), idx.wrapping_add(1)) };
        dev_fence();

        // kick RX
        let addr = self.notify_va + (self.rx_notify_off as u32 * self.notify_mul) as usize;
        unsafe { write_volatile(addr as *mut u16, 0) };
        dev_fence();

        self.rx_last_used = self.rx_last_used.wrapping_add(1);
        copy_len
    }

    pub fn dbg_rx(&self) -> (u16, u16, u8) {
        unsafe {
            let avail = read_volatile(self.rx.avail_idx_ptr());
            let used = read_volatile(self.rx.used_idx_ptr());
            let isr = read_volatile((self.notify_va - 0x2000) as *const u8); // BAR4+0x1000 = ISR
            (avail, used, isr)
        }
    }

    pub fn dbg_rx_buf0(&self) -> [u8; 16] {
        let mut out = [0u8; 16];
        out.copy_from_slice(&self.rx_bufs[0][..16]);
        out
    }
}