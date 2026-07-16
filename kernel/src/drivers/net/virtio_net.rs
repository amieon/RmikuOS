use alloc::vec::Vec;
use core::ptr::{read_volatile, write_volatile};

#[cfg(target_arch = "riscv64")]
use crate::drivers::virtio::transport::mmio::VirtioMmioHeader;
use crate::mm::virt_to_phys;
use crate::drivers::virtio::queue::{VirtioQueue, VIRTQ_DESC_F_NEXT, VIRTQ_DESC_F_WRITE};

pub const NET_QUEUE_SIZE: usize = 8;

// VirtIO PCI 现代布局寄存器偏移（common cfg）
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

impl VirtioNet {
    pub fn init() -> Option<Self> {
        #[cfg(target_arch = "loongarch64")]
        { Self::init_pci() }
        #[cfg(target_arch = "riscv64")]
        { Self::init_mmio() }
    }

    // ============================================================
    //  LoongArch64 — PCI 现代版
    // ============================================================
    #[cfg(target_arch = "loongarch64")]
    fn init_pci() -> Option<Self> {
        use crate::pci::{ecam::enable_pci_device, probe::PciDeviceLocation};
        use crate::drivers::virtio::transport::pci::parse_virtio_pci_caps;
        use pci_regs::*;

        let loc = Self::find_pci()?;
        let addr = loc.addr();
        enable_pci_device(addr);

        let caps = parse_virtio_pci_caps(addr)?;
        let common = caps.common?;
        let notify = caps.notify?;
        let device = caps.device?;

        let common_va = common.va;
        let notify_va = notify.va;
        let notify_mul = caps.notify_off_multiplier;

        // Reset
        unsafe { write_volatile((common_va + DEVICE_STATUS) as *mut u8, 0) };
        while unsafe { read_volatile((common_va + DEVICE_STATUS) as *const u8) } != 0 {}

        // ACK + DRIVER
        unsafe { write_volatile((common_va + DEVICE_STATUS) as *mut u8, 3) };

        // Features
        unsafe {
            write_volatile((common_va + 0x08) as *mut u32, 0);
            write_volatile((common_va + 0x0C) as *mut u32, 0);
            write_volatile((common_va + DEVICE_STATUS) as *mut u8, 11); // FEATURES_OK
        }
        let mut retries = 1000;
        while unsafe { read_volatile((common_va + DEVICE_STATUS) as *const u8) } & 8 == 0 {
            retries -= 1;
            if retries == 0 {
                log::warn!("[virtio-net] PCI device did not accept FEATURES_OK");
                return None;
            }
        }

        // MAC
        let mut mac = [0u8; 6];
        for i in 0..6 {
            mac[i] = unsafe { read_volatile((device.va + i) as *const u8) };
        }

        let (rx, rx_notify_off) = Self::setup_queue_pci(common_va, notify_va, notify_mul, 0)?;
        let (tx, tx_notify_off) = Self::setup_queue_pci(common_va, notify_va, notify_mul, 1)?;

        unsafe { write_volatile((common_va + DEVICE_STATUS) as *mut u8, 15) }; // DRIVER_OK

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
                write_volatile(rx.avail_idx_ptr(), idx.wrapping_add(1));
            }
            rx_bufs.push(buf);
        }
        {
            let addr = notify_va + (rx_notify_off as u32 * notify_mul) as usize;
            unsafe { write_volatile(addr as *mut u16, 0) };
        }

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
                    if info.vendor_id == 0x1AF4 && info.device_id == 0x1041 {
                        return Some(loc);
                    }
                    if function == 0 && (info.header_type & 0x80) == 0 {
                        break;
                    }
                }
            }
        }
        None
    }

    #[cfg(target_arch = "loongarch64")]
    fn setup_queue_pci(
        common_va: usize, notify_va: usize, notify_mul: u32, qid: u16,
    ) -> Option<(VirtioQueue, u16)> {
        use pci_regs::*;
        unsafe { write_volatile((common_va + QUEUE_SELECT) as *mut u16, qid) };
        let max = unsafe { read_volatile((common_va + QUEUE_SIZE_REG) as *const u16) } as usize;
        if max == 0 { return None; }
        let size = if max < NET_QUEUE_SIZE { max } else { NET_QUEUE_SIZE };

        let vq = VirtioQueue::new_modern(size)?;
        unsafe {
            write_volatile((common_va + QUEUE_DESC_LO) as *mut u64, vq.desc_pa as u64);
            write_volatile((common_va + QUEUE_AVAIL_LO) as *mut u64, vq.avail_pa as u64);
            write_volatile((common_va + QUEUE_USED_LO) as *mut u64, vq.used_pa as u64);
            write_volatile((common_va + QUEUE_ENABLE) as *mut u16, 1);
        }
        let notify_off = unsafe { read_volatile((common_va + QUEUE_NOTIFY_OFF) as *const u16) };
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

        let pa = probe_virtio_net_mmio()?;
        let virt_base = crate::mm::kernel_phys_to_virt(pa);
        let hdr = VirtioMmioHeader::new(virt_base);

        if hdr.magic() != 0x7472_6976 || hdr.version() != 2 {
            log::warn!("[virtio-net] MMIO probe failed: bad magic/version");
            return None;
        }

        hdr.reset();

        // ACK + DRIVER
        hdr.set_status_bits(VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

        // Feature negotiation: only VERSION_1
        hdr.write_driver_features(0, VIRTIO_F_VERSION_1 as u32);
        hdr.write_driver_features(1, (VIRTIO_F_VERSION_1 >> 32) as u32);

        hdr.set_status_bits(VIRTIO_STATUS_FEATURES_OK);
        if hdr.status() & VIRTIO_STATUS_FEATURES_OK == 0 {
            log::warn!("[virtio-net] MMIO device did not accept FEATURES_OK");
            hdr.fail();
            return None;
        }

        // MAC at device-specific config offset 0x100
        let mut mac = [0u8; 6];
        for i in 0..6 {
            mac[i] = unsafe { read_volatile((virt_base + 0x100 + i) as *const u8) };
        }

        let rx = Self::setup_queue_mmio(&hdr, 0)?;
        let tx = Self::setup_queue_mmio(&hdr, 1)?;

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
                write_volatile(rx.avail_idx_ptr(), idx.wrapping_add(1));
            }
            rx_bufs.push(buf);
        }
        hdr.notify_queue(0); // kick RX

        // DRIVER_OK
        hdr.set_status_bits(VIRTIO_STATUS_DRIVER_OK);

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
        if max == 0 { return None; }
        let size = if max < NET_QUEUE_SIZE { max } else { NET_QUEUE_SIZE };

        let vq = VirtioQueue::new_modern(size)?;
        hdr.set_queue_size(size as u32);
        hdr.set_queue_desc_addr(vq.desc_pa);
        hdr.set_queue_driver_addr(vq.avail_pa);
        hdr.set_queue_device_addr(vq.used_pa);
        hdr.set_queue_ready(1);
        Some(vq)
    }

    // ============================================================
    //  通用发送 / 接收
    // ============================================================
    pub fn send(&mut self, packet: &[u8]) {
        if packet.len() > 1514 { return; }

        let tx = &self.tx;
        let id1 = 0usize;
        let id2 = 1usize;

        let mut tx_hdr = VirtioNetHdr {
            flags: 0, gso_type: 0, hdr_len: 0,
            gso_size: 0, csum_start: 0, csum_offset: 0, num_buffers: 0,
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
        }

        let used_before = unsafe { read_volatile(tx.used_idx_ptr()) };

        let idx = unsafe { read_volatile(tx.avail_idx_ptr()) };
        unsafe {
            write_volatile(tx.avail_ring_ptr((idx as usize) % tx.size), id1 as u16);
            write_volatile(tx.avail_idx_ptr(), idx.wrapping_add(1));
        }

        // kick TX
        #[cfg(target_arch = "loongarch64")]
        {
            let addr = self.notify_va + (self.tx_notify_off as u32 * self.notify_mul) as usize;
            unsafe { write_volatile(addr as *mut u16, 0) };
        }
        #[cfg(target_arch = "riscv64")]
        {
            self.mmio.notify_queue(1);
        }

        // 同步等待
        let mut spin = 0usize;
        while unsafe { read_volatile(tx.used_idx_ptr()) } == used_before {
            spin += 1;
            if spin > 1_000_000 {
                log::warn!("[virtio-net] TX timeout");
                break;
            }
            core::hint::spin_loop();
        }
    }

    pub fn poll_rx(&mut self, out: &mut [u8]) -> usize {
        let rx = &self.rx;
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
        unsafe {
            let d = &mut *rx.desc_mut(id);
            d.len = 2048;
            d.flags = VIRTQ_DESC_F_WRITE;
            let idx = read_volatile(rx.avail_idx_ptr());
            write_volatile(rx.avail_ring_ptr((idx as usize) % rx.size), id as u16);
            write_volatile(rx.avail_idx_ptr(), idx.wrapping_add(1));
        }

        // kick RX
        #[cfg(target_arch = "loongarch64")]
        {
            let addr = self.notify_va + (self.rx_notify_off as u32 * self.notify_mul) as usize;
            unsafe { write_volatile(addr as *mut u16, 0) };
        }
        #[cfg(target_arch = "riscv64")]
        {
            self.mmio.notify_queue(0);
        }

        self.rx_last_used = self.rx_last_used.wrapping_add(1);
        copy_len
    }
}