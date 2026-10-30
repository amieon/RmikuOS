//! virtio-rng 驱动（virtio device id = 4）
//! 走 PCI 而不是 MMIO：virtio-net 已经证明 riscv64 virt 和 loongarch64 virt
//! 都有可用的 PCIe，统一一条路径就不用处理 virtio-mmio 地址反序那套。

use core::ptr::{read_volatile, write_volatile};

use crate::drivers::virtio::queue::{VirtioQueue, VIRTQ_DESC_F_WRITE};
use crate::mm::{alloc_frame, kernel_phys_to_virt, PhysPageNum, PAGE_SIZE_BITS};
use crate::mm::config::PAGE_SIZE;

/// modern = 0x1040 + 4；transitional 是 0x1005
const PCI_DEVICE_ID_VIRTIO_RNG_MODERN: u16 = 0x1044;
const PCI_DEVICE_ID_VIRTIO_RNG_TRANSITIONAL: u16 = 0x1005;

const RNG_QUEUE_SIZE: usize = 8;

/// DMA 缓冲固定一页。用 alloc_frame 拿连续物理页，而不是拿堆上的 Vec：
/// 堆指针要先 virt_to_phys，而它一旦跨页，设备就会读到半块错的内存。
const RNG_BUF_SIZE: usize = PAGE_SIZE;

/// 自旋上限。QEMU 下一次请求是微秒级；这个上限只是防止设备没起来时死等。
const RNG_TIMEOUT_SPINS: usize = 4_000_000;

// virtio-pci common configuration 寄存器偏移（与 virtio_net 同一套）
mod pci_regs {
    pub const DEVICE_STATUS: usize = 0x14;
    pub const QUEUE_SELECT: usize = 0x16;
    pub const QUEUE_SIZE_REG: usize = 0x18;
    pub const QUEUE_ENABLE: usize = 0x1C;
    pub const QUEUE_NOTIFY_OFF: usize = 0x1E;
    pub const QUEUE_DESC_LO: usize = 0x20;
    pub const QUEUE_AVAIL_LO: usize = 0x28;
    pub const QUEUE_USED_LO: usize = 0x30;
}

#[inline]
fn dev_fence() {
    core::sync::atomic::fence(core::sync::atomic::Ordering::SeqCst);
}

pub struct VirtioRng {
    queue: VirtioQueue,
    /// DMA 缓冲（一页），设备直接往里写
    buf: PhysPageNum,
    common_va: usize,
    notify_va: usize,
    notify_off: u16,
    notify_mul: u32,
    /// 自己记账的 used ring 游标。**不能直接读 used.idx 当"轮次"用**：
    /// 那是设备侧的累计完成数，只有和自己的期望值配对比较才有意义。
    used_seen: u16,
    /// 一次超时之后整个设备就不可信了：那一笔请求可能在之后才被设备完成，
    /// used ring 里会突然多出一项，下一笔就会把**上一笔的垃圾**当成新随机数。
    /// 与其尝试重同步（时序问题无法在驱动侧证明正确），不如直接停用。
    poisoned: bool,
}

impl VirtioRng {
    pub fn init() -> Option<Self> {
        use crate::drivers::virtio::transport::pci::parse_virtio_pci_caps;
        use crate::pci::ecam::enable_pci_device;
        use pci_regs::*;

        let loc = Self::find_pci()?;
        let addr = loc.addr();

        crate::pci::bar::assign_all_bars(addr);
        enable_pci_device(addr);

        let caps = parse_virtio_pci_caps(addr)?;
        let common = caps.common.as_ref()?;
        let notify = caps.notify.as_ref()?;
        let common_va = common.va;
        let notify_va = notify.va;
        let notify_mul = caps.notify_off_multiplier;

        // ---- reset ----
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

        // ---- features：只协商 VIRTIO_F_VERSION_1 ----
        // rng 没有设备专有 feature（VIRTIO_RNG_F_* 不存在），
        // 但 modern 设备（disable-legacy=on）必须显式协商 VERSION_1。
        unsafe {
            write_volatile((common_va + 0x08) as *mut u32, 0);
            dev_fence();
            write_volatile((common_va + 0x0C) as *mut u32, 0);
            dev_fence();
            write_volatile((common_va + 0x08) as *mut u32, 1);
            dev_fence();
            write_volatile((common_va + 0x0C) as *mut u32, 1);
            dev_fence();
        }

        // ---- FEATURES_OK ----
        unsafe { write_volatile((common_va + DEVICE_STATUS) as *mut u8, 11) };
        dev_fence();
        let mut retries = 1000;
        loop {
            let s = unsafe { read_volatile((common_va + DEVICE_STATUS) as *const u8) };
            if s & 8 != 0 {
                break;
            }
            retries -= 1;
            if retries == 0 {
                log::warn!("[rng] FEATURES_OK timeout, status={:#x}", s);
                return None;
            }
            dev_fence();
        }

        // ---- queue 0 ----
        let (queue, notify_off) = Self::setup_queue(common_va, 0)?;

        let buf = alloc_frame()?;
        unsafe {
            core::ptr::write_bytes(kernel_phys_to_virt(buf.0 << PAGE_SIZE_BITS) as *mut u8, 0, PAGE_SIZE);
        }

        unsafe { write_volatile((common_va + DEVICE_STATUS) as *mut u8, 15) }; // DRIVER_OK
        dev_fence();

        // 我们纯轮询，告诉设备别发中断（省一次进 trap，也不必实现 ISR ack）
        unsafe {
            write_volatile(queue.avail_flags_ptr(), 1); // VIRTQ_AVAIL_F_NO_INTERRUPT
        }
        dev_fence();

        log::info!(
            "[rng] virtio-rng ready: q_pa={:#x} size={} buf_pa={:#x} notify_off={} mul={}",
            queue.queue_pa,
            queue.size,
            buf.0 << PAGE_SIZE_BITS,
            notify_off,
            notify_mul,
        );

        Some(Self {
            queue,
            buf,
            common_va,
            notify_va,
            notify_off,
            notify_mul,
            used_seen: 0,
            poisoned: false,
        })
    }

    fn find_pci() -> Option<crate::pci::probe::PciDeviceLocation> {
        use crate::pci::probe::{read_device_info, PciDeviceLocation};

        for bus in 0u8..=0 {
            for device in 0u8..32 {
                for function in 0u8..8 {
                    let loc = PciDeviceLocation { bus, device, function };
                    let Some(info) = read_device_info(loc) else {
                        if function == 0 {
                            break;
                        }
                        continue;
                    };
                    if info.vendor_id == 0x1AF4
                        && (info.device_id == PCI_DEVICE_ID_VIRTIO_RNG_MODERN
                            || info.device_id == PCI_DEVICE_ID_VIRTIO_RNG_TRANSITIONAL)
                    {
                        log::info!(
                            "[rng] found 1AF4:{:04x} at bus={} dev={} fn={}",
                            info.device_id,
                            bus,
                            device,
                            function,
                        );
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

    fn setup_queue(common_va: usize, qid: u16) -> Option<(VirtioQueue, u16)> {
        use pci_regs::*;

        unsafe { write_volatile((common_va + QUEUE_SELECT) as *mut u16, qid) };
        dev_fence();
        let max = unsafe { read_volatile((common_va + QUEUE_SIZE_REG) as *const u16) } as usize;
        dev_fence();
        if max == 0 {
            log::warn!("[rng] queue{}: max=0, device not present?", qid);
            return None;
        }
        let size = max.min(RNG_QUEUE_SIZE);

        let vq = VirtioQueue::new_modern(size)?;
        unsafe {
            write_volatile((common_va + QUEUE_SIZE_REG) as *mut u16, size as u16);
            dev_fence();
            // common cfg 的 max_access_size 是 4，u64 必须拆两个 u32 写
            let wr64 = |off: usize, val: u64| {
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

        let notify_off = unsafe { read_volatile((common_va + QUEUE_NOTIFY_OFF) as *const u16) };
        dev_fence();
        // 注意：这里**不做** "notify_off==0 就退化成 qid" 那种兜底。
        // queue 0 的 notify_off 本来就可能是 0，盲目兜底反而会写错地址。
        Some((vq, notify_off))
    }

    /// 发一次请求，把设备填好的字节拷进 `dst`。
    /// 返回实际拿到的字节数；超时或异常返回 None。
    fn request(&mut self, want: usize, dst: &mut [u8]) -> Option<usize> {
        debug_assert!(want <= RNG_BUF_SIZE);
        if self.poisoned {
            return None;
        }

        let q = &self.queue;
        let buf_pa = self.buf.0 << PAGE_SIZE_BITS;

        unsafe {
            let d = &mut *q.desc_mut(0);
            d.addr = buf_pa as u64;
            d.len = want as u32;
            // VIRTQ_DESC_F_WRITE = 设备写、驱动读。写反了设备会把随机字节当输入，直接没结果
            d.flags = VIRTQ_DESC_F_WRITE;
            d.next = 0;
        }
        dev_fence();

        let seen_before = self.used_seen;

        unsafe {
            let idx = read_volatile(q.avail_idx_ptr());
            write_volatile(q.avail_ring_ptr((idx as usize) % q.size), 0u16);
            dev_fence();
            write_volatile(q.avail_idx_ptr(), idx.wrapping_add(1));
        }
        dev_fence();

        let naddr = self.notify_va + (self.notify_off as u32 * self.notify_mul) as usize;
        unsafe { write_volatile(naddr as *mut u16, 0) };
        dev_fence();

        let mut spins = 0usize;
        loop {
            dev_fence();
            let used = unsafe { read_volatile(q.used_idx_ptr()) };
            if used != seen_before {
                let elem = unsafe { &*q.used_ring_ptr((seen_before as usize) % q.size) };
                let got = core::cmp::min(elem.len as usize, want);
                self.used_seen = seen_before.wrapping_add(1);

                if got == 0 {
                    return None;
                }
                unsafe {
                    let src = kernel_phys_to_virt(buf_pa) as *const u8;
                    core::ptr::copy_nonoverlapping(src, dst.as_mut_ptr(), got);
                }
                return Some(got);
            }
            spins += 1;
            if spins > RNG_TIMEOUT_SPINS {
                self.poisoned = true;
                log::warn!("[rng] request timeout (want={}), device disabled", want);
                return None;
            }
            core::hint::spin_loop();
        }
    }

    /// 填满 `out`。任何一次短读/超时都整条失败 —— 半截成功比失败更危险，
    /// 调用方会以为缓冲里全是随机字节。
    pub fn fill_bytes(&mut self, out: &mut [u8]) -> bool {
        let mut done = 0usize;
        while done < out.len() {
            let want = core::cmp::min(out.len() - done, RNG_BUF_SIZE);
            let Some(n) = self.request(want, &mut out[done..done + want]) else {
                return false;
            };
            done += n;
        }
        true
    }
}
