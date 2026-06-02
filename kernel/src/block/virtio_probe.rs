use super::virtio_mmio::VirtioMmioHeader;

const VIRTIO_MAGIC: u32 = 0x7472_6976;
const VIRTIO_DEVICE_ID_BLOCK: u32 = 2;

#[cfg(target_arch = "riscv64")]
const VIRTIO_MMIO_BASE: usize = 0x1000_1000;

#[cfg(target_arch = "riscv64")]
const VIRTIO_MMIO_STRIDE: usize = 0x1000;

#[cfg(target_arch = "riscv64")]
const VIRTIO_MMIO_COUNT: usize = 8;

pub fn probe_virtio_blk_mmio() -> Option<usize> {
    #[cfg(target_arch = "riscv64")]
    {
        for i in 0..crate::arch::VIRTIO_MMIO_COUNT {
            let phys_base =
                crate::arch::VIRTIO_MMIO_BASE
                + i * crate::arch::VIRTIO_MMIO_STRIDE;

            let virt_base = crate::mm::kernel_phys_to_virt(phys_base);

            let hdr = super::virtio_mmio::VirtioMmioHeader::new(virt_base);

            let magic = hdr.magic();
            let version = hdr.version();
            let device_id = hdr.device_id();
            let vendor_id = hdr.vendor_id();

            if magic != super::virtio_mmio::VIRTIO_MAGIC {
                continue;
            }

            log::info!(
                "[virtio] mmio slot {} pa={:#x}, va={:#x}, version={}, device_id={}, vendor={:#x}",
                i,
                phys_base,
                virt_base,
                version,
                device_id,
                vendor_id,
            );

            if device_id == super::virtio_mmio::VIRTIO_DEVICE_ID_BLOCK {
                log::info!(
                    "[virtio] found block device at pa={:#x}, va={:#x}",
                    phys_base,
                    virt_base,
                );

                return Some(phys_base);
            }
        }

        log::warn!("[virtio] no virtio-blk mmio device found");
        None
    }

    #[cfg(not(target_arch = "riscv64"))]
    {
        log::warn!("[virtio] mmio probe not implemented for this arch");
        None
    }
}