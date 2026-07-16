use alloc::vec::Vec;
use core::ptr::{read_volatile, write_volatile};

#[cfg(target_arch = "riscv64")]
use crate::drivers::virtio::transport::mmio::VirtioMmioHeader;
use crate::mm::virt_to_phys;
use crate::drivers::virtio::queue::{VirtioQueue, VIRTQ_DESC_F_NEXT, VIRTQ_DESC_F_WRITE};

pub const NET_QUEUE_SIZE: usize = 8;



#[cfg(target_arch = "loongarch64")]
mod pci_regs {
    pub const DEVICE_STATUS:      usize = 0x14;
    pub const QUEUE_SELECT:       usize = 0x16;
    pub const QUEUE_SIZE_REG:     usize = 0x18;
    pub const QUEUE_ENABLE:       usize = 0x1C;
    pub const QUEUE_NOTIFY_OFF:   usize = 0x1E;
    pub const QUEUE_DESC_LO:      usize = 0x20;
    pub const QUEUE_AVAIL_LO:     usize = 0x28;
    pub const QUEUE_USED_LO:      usize = 0x30;
}

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

    #[cfg(target_arch = "loongarch64")]
    common_va: usize,
    #[cfg(target_arch = "loongarch64")]
    notify_va: usize,
    #[cfg(target_arch = "loongarch64")]
    notify_mul: u32,
    #[cfg(target_arch = "loongarch64")]
    rx_notify_off: u16,
    #[cfg(target_arch = "loongarch64")]
    tx_notify_off: u16,

    #[cfg(target_arch = "riscv64")]
    mmio: crate::drivers::virtio::transport::mmio::VirtioMmioHeader,
}

/// LoongArch 读设备寄存器前刷新缓存
#[cfg(target_arch = "loongarch64")]
#[inline]
fn dev_fence() {
    core::sync::atomic::fence(core::sync::atomic::Ordering::SeqCst);
}

#[cfg(not(target_arch = "loongarch64"))]
#[inline]
fn dev_fence() {}

impl VirtioNet {
    pub fn init() -> Option<Self> {
        #[cfg(target_arch = "loongarch64")]
        { Self::init_pci() }
        #[cfg(target_arch = "riscv64")]
        { Self::init_mmio() }
    }

    // ============================================================
    //  LoongArch64 — PCI
    // ============================================================
    #[cfg(target_arch = "loongarch64")]
    fn init_pci() -> Option<Self> {
        use crate::pci::{ecam::enable_pci_device, probe::PciDeviceLocation};
            
        use crate::drivers::virtio::transport::pci::parse_virtio_pci_caps;
        use pci_regs::*;

        log::info!("[virtio-net] PCI: scanning for device...");
        let loc = Self::find_pci()?;
        let addr = loc.addr();

        crate::pci::bar::assign_all_bars(addr);

        enable_pci_device(addr);                       // 再开 Memory Space + Bus Master


        let caps = parse_virtio_pci_caps(addr)?;        // 这时读到的 BAR4 才是真地址
        log::info!("[virtio-net] PCI: caps parsed ok");

        let common = caps.common.as_ref()?;
        let notify = caps.notify.as_ref()?;
        let device = caps.device.as_ref()?;
        log::info!("[virtio-net] PCI: common={:#x} notify={:#x} device={:#x}", common.va, notify.va, device.va);

        let common_va = common.va;
        let notify_va = notify.va;
        let notify_mul = caps.notify_off_multiplier;

        // Reset
        // ---- Reset ----
        unsafe { write_volatile((common_va + DEVICE_STATUS) as *mut u8, 0) };
        dev_fence();
        while unsafe { read_volatile((common_va + DEVICE_STATUS) as *const u8) } != 0 {
            dev_fence();
        }
        log::info!("[dbg] reset done, status={:#x}",
            unsafe { read_volatile((common_va + DEVICE_STATUS) as *const u8) });

        // 设备身份确认：num_queues 必须是 2（RX/TX）
        log::info!("[dbg] num_queues={}",
            unsafe { read_volatile((common_va + 0x12) as *const u16) });

        // ---- ACK -> DRIVER，每步读回 ----
        unsafe { write_volatile((common_va + DEVICE_STATUS) as *mut u8, 1) };
        dev_fence();
        log::info!("[dbg] after ACK:    status={:#x}",
            unsafe { read_volatile((common_va + DEVICE_STATUS) as *const u8) });

        unsafe { write_volatile((common_va + DEVICE_STATUS) as *mut u8, 3) };
        dev_fence();
        log::info!("[dbg] after DRIVER: status={:#x}",
            unsafe { read_volatile((common_va + DEVICE_STATUS) as *const u8) });

        // ---- 设备提供的 features（0x00=select, 0x04=device_feature 只读）----
        unsafe {
            write_volatile((common_va + 0x00) as *mut u32, 0);
            dev_fence();
            let lo = read_volatile((common_va + 0x04) as *const u32);
            write_volatile((common_va + 0x00) as *mut u32, 1);
            dev_fence();
            let hi = read_volatile((common_va + 0x04) as *const u32);
            log::info!("[dbg] device_feature lo={:#010x} hi={:#010x}", lo, hi);
            // hi 的 bit0 必须是 1（VERSION_1），modern 设备一定提供
        }

        // ---- 写 driver features：只要 VERSION_1 ----
        unsafe {
            write_volatile((common_va + 0x08) as *mut u32, 0);  // driver_feature_select = 0
            dev_fence();
            write_volatile((common_va + 0x0C) as *mut u32, 1 << 15);  // 低 32 位全不要
            dev_fence();
            write_volatile((common_va + 0x08) as *mut u32, 1);  // select = 1
            dev_fence();
            write_volatile((common_va + 0x0C) as *mut u32, 1);  // VERSION_1
            dev_fence();

            // 回读验证（注意：映射是 cached 的话这里会说谎）
            write_volatile((common_va + 0x08) as *mut u32, 0);
            dev_fence();
            let r_lo = read_volatile((common_va + 0x0C) as *const u32);
            write_volatile((common_va + 0x08) as *mut u32, 1);
            dev_fence();
            let r_hi = read_volatile((common_va + 0x0C) as *const u32);
            log::info!("[dbg] driver_feature readback lo={:#x} hi={:#x}", r_lo, r_hi);
        }

        // ---- FEATURES_OK ----
        unsafe { write_volatile((common_va + DEVICE_STATUS) as *mut u8, 11) };
        dev_fence();
        for i in 0..5 {
            dev_fence();
            let s = unsafe { read_volatile((common_va + DEVICE_STATUS) as *const u8) };
            log::info!("[dbg] FEATURES_OK poll[{}]: status={:#x}", i, s);
        }

        // 读 MAC；如果 device config 读出来全 0，用 QEMU 默认 MAC
        let mut mac = [0u8; 6];
        for i in 0..6 {
            dev_fence();
            mac[i] = unsafe { read_volatile((device.va + i) as *const u8) };
        }
        if mac == [0u8; 6] {
            mac = [0x52, 0x54, 0x00, 0x12, 0x34, 0x56];
            log::info!("[virtio-net] PCI: device MAC is zero, using default {:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        } else {
            log::info!("[virtio-net] PCI: MAC={:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        }

        // 建队列
        let (rx, rx_notify_off) = Self::setup_queue_pci(common_va, notify_va, notify_mul, 0)?;
        log::info!("[virtio-net] PCI: RX queue ready, size={} notify_off={}", rx.size, rx_notify_off);
        let (tx, tx_notify_off) = Self::setup_queue_pci(common_va, notify_va, notify_mul, 1)?;
        log::info!("[virtio-net] PCI: TX queue ready, size={} notify_off={}", tx.size, tx_notify_off);

        unsafe { write_volatile((common_va + DEVICE_STATUS) as *mut u8, 15) }; // DRIVER_OK
        dev_fence();
        log::info!("[virtio-net] PCI: DRIVER_OK");

        // RX pre-fill
        let mut rx_bufs: Vec<Vec<u8>> = Vec::new();
        for i in 0..rx.size {
            let mut buf = alloc::vec![0u8; 2048];
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
        log::info!("[net] rxq pa={:#x} txq pa={:#x}", rx.queue_pa, tx.queue_pa);
        log::info!("[net] rx_buf0 pa={:#x}", virt_to_phys(rx_bufs[0].as_ptr() as usize));
        log::info!("[virtio-net] PCI: RX buffers filled, kicked at {:#x}", notify_va + (rx_notify_off as u32 * notify_mul) as usize);

        Some(Self {
            common_va, notify_va, notify_mul,
            rx, tx, mac, rx_bufs,
            rx_last_used: 0,
            rx_notify_off, tx_notify_off,
        })
    }

    #[cfg(target_arch = "loongarch64")]
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

    #[cfg(target_arch = "loongarch64")]
    fn setup_queue_pci(
        common_va: usize, notify_va: usize, notify_mul: u32, qid: u16,
    ) -> Option<(VirtioQueue, u16)> {
        use pci_regs::*;
        unsafe { write_volatile((common_va + QUEUE_SELECT) as *mut u16, qid) };
        dev_fence();
        let max = unsafe { read_volatile((common_va + QUEUE_SIZE_REG) as *const u16) } as usize;
        dev_fence();
        log::info!("[virtio-net] setup_queue_pci qid={} raw_max_size={}", qid, max);

        let size = if max == 0 {
            log::info!("[virtio-net] setup_queue_pci qid={}: max_size=0, trying fallback 256", qid);
            256
        } else if max < NET_QUEUE_SIZE {
            max
        } else {
            NET_QUEUE_SIZE
        };

        let vq = VirtioQueue::new_modern(size)?;
        // setup_queue_pci 里，替换三个 u64 写：
        unsafe {
            write_volatile((common_va + QUEUE_SIZE_REG) as *mut u16, size as u16);
            dev_fence();
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
            log::warn!("[virtio-net] setup_queue_pci qid={}: notify_off=0, fallback to {}", qid, qid);
            notify_off = qid as u16;
        }
        Some((vq, notify_off))
    }

    // ============================================================
    //  RISC-V64 — MMIO
    // ============================================================
    #[cfg(target_arch = "riscv64")]
    fn init_mmio() -> Option<Self> {
        use crate::drivers::virtio::transport::mmio::{
            probe_virtio_net_mmio, VirtioMmioHeader,
            VIRTIO_STATUS_ACKNOWLEDGE, VIRTIO_STATUS_DRIVER,
            VIRTIO_STATUS_FEATURES_OK, VIRTIO_STATUS_DRIVER_OK,
            VIRTIO_F_VERSION_1,
        };

        log::info!("[virtio-net] MMIO: probing...");
        let pa = probe_virtio_net_mmio()?;
        log::info!("[virtio-net] MMIO: found at pa={:#x}", pa);

        let virt_base = crate::mm::kernel_phys_to_virt(pa);
        let hdr = VirtioMmioHeader::new(virt_base);

        let magic = hdr.magic();
        let version = hdr.version();
        let dev_id = hdr.device_id();
        log::info!("[virtio-net] MMIO: magic={:#x} version={} device_id={}", magic, version, dev_id);

        if magic != 0x7472_6976 {
            log::warn!("[virtio-net] MMIO: bad magic, expected 0x74726976");
            return None;
        }
        if version != 2 {
            log::warn!("[virtio-net] MMIO: bad version {}, expected 2", version);
            return None;
        }
        if dev_id != 1 {
            log::warn!("[virtio-net] MMIO: bad device_id {}, expected 1 (net)", dev_id);
            return None;
        }

        hdr.reset();
        log::info!("[virtio-net] MMIO: reset done");

        // ACK + DRIVER
        hdr.set_status_bits(VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

        // Feature negotiation
        hdr.write_driver_features(0, VIRTIO_F_VERSION_1 as u32);
        hdr.write_driver_features(1, (VIRTIO_F_VERSION_1 >> 32) as u32);

        hdr.set_status_bits(VIRTIO_STATUS_FEATURES_OK);
        if hdr.status() & VIRTIO_STATUS_FEATURES_OK == 0 {
            log::warn!("[virtio-net] MMIO: FEATURES_OK not accepted");
            hdr.fail();
            return None;
        }
        log::info!("[virtio-net] MMIO: FEATURES_OK accepted");

        // MAC at offset 0x100
        let mut mac = [0u8; 6];
        for i in 0..6 {
            mac[i] = unsafe { read_volatile((virt_base + 0x100 + i) as *const u8) };
        }
        if mac == [0u8; 6] {
            mac = [0x52, 0x54, 0x00, 0x12, 0x34, 0x56];
            log::warn!("[virtio-net] MMIO: device MAC is zero, using default");
        } else {
            log::info!("[virtio-net] MMIO: MAC={:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        }

        let rx = Self::setup_queue_mmio(&hdr, 0)?;
        log::info!("[virtio-net] MMIO: RX queue ready");
        let tx = Self::setup_queue_mmio(&hdr, 1)?;
        log::info!("[virtio-net] MMIO: TX queue ready");

        // RX pre-fill
        let mut rx_bufs: Vec<Vec<u8>> = Vec::new();
        for i in 0..rx.size {
            let mut buf = alloc::vec![0u8; 2048];
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
            core::sync::atomic::fence(core::sync::atomic::Ordering::SeqCst);
            unsafe {
                let idx = read_volatile(rx.avail_idx_ptr());
                write_volatile(rx.avail_idx_ptr(), idx.wrapping_add(1));
            }
            core::sync::atomic::fence(core::sync::atomic::Ordering::SeqCst);
            rx_bufs.push(buf);
        }
        hdr.notify_queue(0);
        log::info!("[virtio-net] MMIO: RX buffers filled, kicked");

        hdr.set_status_bits(VIRTIO_STATUS_DRIVER_OK);
        log::info!("[virtio-net] MMIO: DRIVER_OK");

        Some(Self {
            mmio: hdr,
            rx, tx, mac, rx_bufs,
            rx_last_used: 0,
        })
    }

    #[cfg(target_arch = "riscv64")]
    fn setup_queue_mmio(hdr: &VirtioMmioHeader, qid: u16) -> Option<VirtioQueue> {
        hdr.select_queue(qid as u32);
        let max = hdr.queue_size_max() as usize;
        log::info!("[virtio-net] setup_queue_mmio qid={} max_size={}", qid, max);
        if max == 0 {
            log::warn!("[virtio-net] setup_queue_mmio qid={}: max_size=0", qid);
            return None;
        }
        let size = max.min(NET_QUEUE_SIZE);

        let vq = VirtioQueue::new_modern(size)?;
        hdr.set_queue_size(size as u32);
        hdr.set_queue_desc_addr(vq.desc_pa);
        hdr.set_queue_driver_addr(vq.avail_pa);
        hdr.set_queue_device_addr(vq.used_pa);
        hdr.set_queue_ready(1);
        log::info!("[virtio-net] setup_queue_mmio qid={}: size={} desc_pa={:#x}", qid, size, vq.desc_pa);
        Some(vq)
    }


    pub fn send(&mut self, packet: &[u8]) {
        if packet.len() > 1514 { return; }

        let tx = &self.tx;
        let id1 = 0usize;
        let id2 = 1usize;

        let mut tx_hdr = VirtioNetHdr {
            flags: 0, gso_type: 0, hdr_len: 0,
            gso_size: 0, csum_start: 0, csum_offset: 0,num_buffers: 0,
        };
        let hdr_p = virt_to_phys(&mut tx_hdr as *mut _ as usize);

        unsafe {
            let d1 = &mut *tx.desc_mut(id1);
            d1.addr = hdr_p as u64;
            d1.len = core::mem::size_of::<VirtioNetHdr>() as u32;
            d1.flags = VIRTQ_DESC_F_NEXT;
            d1.next = id2 as u16;

            let d2 = &mut *tx.desc_mut(id2);
            d2.addr = virt_to_phys(packet.as_ptr() as usize) as u64;
            d2.len = packet.len() as u32;
            d2.flags = 0;
            d2.next = 0;
            log::info!("[tx] d1_len={} packet_len={}", d1.len, packet.len());
// 必须看到 d1_len=10
        }

        dev_fence();
        let used_before = unsafe { read_volatile(tx.used_idx_ptr()) };
        dev_fence();

        let idx = unsafe { read_volatile(tx.avail_idx_ptr()) };
        unsafe {
            write_volatile(tx.avail_ring_ptr((idx as usize) % tx.size), id1 as u16);
        }
        dev_fence();
        unsafe {
            write_volatile(tx.avail_idx_ptr(), idx.wrapping_add(1));
        }
        dev_fence();
        log::info!("[tx] {:02x?}", packet);

        // kick TX
        #[cfg(target_arch = "loongarch64")]
        {
            let addr = self.notify_va + (self.tx_notify_off as u32 * self.notify_mul) as usize;
            unsafe { write_volatile(addr as *mut u16, 1) };
            dev_fence();
        }
        #[cfg(target_arch = "riscv64")]
        {
            self.mmio.notify_queue(1);
        }

        // 同步等待：每次读 used_idx 前 fence 刷新缓存
        let mut spin = 0usize;
        loop {
            dev_fence();
            let used_now = unsafe { read_volatile(tx.used_idx_ptr()) };
            if used_now != used_before {
                log::info!("[virtio-net] TX completed: used_before={} used_now={}", used_before, used_now);
                break;
            }
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

        // 回收
        let idx = unsafe { read_volatile(rx.avail_idx_ptr()) };
        unsafe {
            write_volatile(rx.avail_ring_ptr((idx as usize) % rx.size), id as u16);
        }
        dev_fence();
        unsafe {
            write_volatile(rx.avail_idx_ptr(), idx.wrapping_add(1));
        }
        dev_fence();

        // kick RX
        #[cfg(target_arch = "loongarch64")]
        {
            let addr = self.notify_va + (self.rx_notify_off as u32 * self.notify_mul) as usize;
            unsafe { write_volatile(addr as *mut u16, 0) };
            dev_fence();
        }
        #[cfg(target_arch = "riscv64")]
        {
            self.mmio.notify_queue(0);
        }

        self.rx_last_used = self.rx_last_used.wrapping_add(1);
        copy_len
    }
}


impl VirtioNet {
// VirtioNet 里加：
pub fn dbg_rx(&self) -> (u16, u16, u8) {
    unsafe {
        let avail = read_volatile(self.rx.avail_idx_ptr());
        let used = read_volatile(self.rx.used_idx_ptr());
        let isr = read_volatile((self.notify_va - 0x2000) as *const u8); // ISR_CFG = notify - 0x2000
        (avail, used, isr)
    }
}
// VirtioNet 里加：
pub fn dbg_rx_buf0(&self) -> [u8; 16] {
    let mut out = [0u8; 16];
    out.copy_from_slice(&self.rx_bufs[0][..16]);
    out
}
}