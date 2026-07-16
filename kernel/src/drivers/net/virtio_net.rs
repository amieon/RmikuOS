use alloc::vec::Vec;
use core::ptr::{read_volatile, write_volatile};

use crate::mm::virt_to_phys;
use crate::drivers::virtio::queue::{VirtioQueue, VirtqDesc, VIRTQ_DESC_F_NEXT, VIRTQ_DESC_F_WRITE};
#[cfg(target_arch = "loongarch64")]
use crate::pci;

pub const NET_QUEUE_SIZE: usize = 8;

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
    common_va: usize,
    #[cfg(target_arch = "loongarch64")]
    notify_va: usize,
    #[cfg(target_arch = "loongarch64")]
    notify_mul: u32,

    rx: VirtioQueue,
    tx: VirtioQueue,
    pub mac: [u8; 6],
    rx_bufs: Vec<Vec<u8>>,
    tx_hdr: VirtioNetHdr,
    rx_last_used: u16,
}

impl VirtioNet {
    pub fn init() -> Option<Self> {
        #[cfg(target_arch = "loongarch64")]
        {
            Self::init_pci()
        }
        #[cfg(target_arch = "riscv64")]
        {
            Self::init_mmio()
        }
    }

    #[cfg(target_arch = "loongarch64")]
    fn init_pci() -> Option<Self> {
        use crate::pci::{ecam::enable_pci_device, probe::PciDeviceLocation};
        use crate::pci::bar::read_bar;
        use crate::drivers::virtio::transport::pci::parse_virtio_pci_caps;

        // 找 virtio-net
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
        unsafe { write_volatile((common_va + 0x14) as *mut u8, 0) };
        while unsafe { read_volatile((common_va + 0x14) as *const u8) } != 0 {}

        // ACK + DRIVER
        unsafe { write_volatile((common_va + 0x14) as *mut u8, 3) };

        // Features (accept none)
        unsafe {
            write_volatile((common_va + 0x08) as *mut u32, 0);
            write_volatile((common_va + 0x0C) as *mut u32, 0);
            write_volatile((common_va + 0x14) as *mut u8, 11); // FEATURES_OK
        }

        // Read MAC
        let mut mac = [0u8; 6];
        for i in 0..6 {
            mac[i] = unsafe { read_volatile((device.va + i) as *const u8) };
        }

        let rx = Self::setup_queue_pci(common_va, notify_va, notify_mul, 0)?;
        let tx = Self::setup_queue_pci(common_va, notify_va, notify_mul, 1)?;

        // DRIVER_OK
        unsafe { write_volatile((common_va + 0x14) as *mut u8, 15) };

        Some(Self {
            common_va, notify_va, notify_mul,
            rx, tx, mac,
            rx_bufs: Vec::new(),
            tx_hdr: VirtioNetHdr { flags:0, gso_type:0, hdr_len:0, gso_size:0, csum_start:0, csum_offset:0, num_buffers:0 },
            rx_last_used: 0,
        })
    }

    #[cfg(target_arch = "loongarch64")]
    fn find_pci() -> Option<crate::pci::probe::PciDeviceLocation> {
        use crate::pci::probe::{read_device_info, PciDeviceLocation};
        use crate::pci::ecam::read_config_u8;

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
    fn setup_queue_pci(common_va: usize, notify_va: usize, notify_mul: u32, qid: u16) -> Option<VirtioQueue> {
        unsafe { write_volatile((common_va + 0x16) as *mut u16, qid) };
        let max = unsafe { read_volatile((common_va + 0x18) as *const u16) } as usize;
        let size = if max == 0 { return None; } else if max < NET_QUEUE_SIZE { max } else { NET_QUEUE_SIZE };

        let vq = VirtioQueue::new_modern(size)?;
        unsafe {
            write_volatile((common_va + 0x20) as *mut u64, vq.desc_pa as u64);
            write_volatile((common_va + 0x28) as *mut u64, vq.avail_pa as u64);
            write_volatile((common_va + 0x30) as *mut u64, vq.used_pa as u64);
            write_volatile((common_va + 0x1C) as *mut u16, 1);
        }

        if qid == 0 {
            // RX: pre-fill
            for i in 0..size {
                let mut buf = Vec::with_capacity(2048);
                buf.resize(2048, 0);
                let paddr = virt_to_phys(buf.as_ptr() as usize);
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
            let notify_off = unsafe { read_volatile((common_va + 0x1E) as *const u16) };
            let addr = notify_va + (notify_off as u32 * notify_mul) as usize;
            unsafe { write_volatile(addr as *mut u16, qid) };
        }

        Some(vq)
    }

    #[cfg(target_arch = "riscv64")]
    fn init_mmio() -> Option<Self> {
        // TODO: 等你需要 riscv 时补上，和 blk 类似
        None
    }

    pub fn send(&mut self, packet: &[u8]) {
        if packet.len() > 1514 { return; }

        let tx = &self.tx;
        let id1 = 0usize;
        let id2 = 1usize;

        let hdr_v = &mut self.tx_hdr as *mut _ as usize;
        let hdr_p = virt_to_phys(hdr_v);

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

        let idx = unsafe { read_volatile(tx.avail_idx_ptr()) };
        unsafe { write_volatile(tx.avail_ring_ptr((idx as usize) % tx.size), id1 as u16) };
        unsafe { write_volatile(tx.avail_idx_ptr(), idx + 1) };

        // kick
        #[cfg(target_arch = "loongarch64")]
        {
            let notify_off = unsafe { read_volatile((self.common_va + 0x1E) as *const u16) };
            let addr = self.notify_va + (notify_off as u32 * self.notify_mul) as usize;
            unsafe { write_volatile(addr as *mut u16, 1) };
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
        }
        let idx = unsafe { read_volatile(rx.avail_idx_ptr()) };
        unsafe { write_volatile(rx.avail_ring_ptr((idx as usize) % rx.size), id as u16) };
        unsafe { write_volatile(rx.avail_idx_ptr(), idx + 1) };

        // kick
        #[cfg(target_arch = "loongarch64")]
        {
            let notify_off = unsafe { read_volatile((self.common_va + 0x1E) as *const u16) };
            let addr = self.notify_va + (notify_off as u32 * self.notify_mul) as usize;
            unsafe { write_volatile(addr as *mut u16, 0) };
        }

        self.rx_last_used += 1;
        copy_len
    }
}