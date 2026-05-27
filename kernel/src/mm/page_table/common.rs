use crate::mm::{PAGE_SIZE_BITS, PhysPageNum, config::PAGE_SIZE, dealloc_frame, kernel_phys_to_virt};

pub struct FrameTracker {
    pub ppn: PhysPageNum,
}

impl FrameTracker {
    pub fn new(ppn: PhysPageNum) -> Self {
        let pa = ppn.0 << PAGE_SIZE_BITS;
        let va = kernel_phys_to_virt(pa);

        assert!(ppn.0 != 0, "FrameTracker::new: ppn is zero");
        assert!(pa != 0, "FrameTracker::new: pa is zero");
        assert!(
            va >= crate::mm::KERNEL_OFFSET,
            "FrameTracker::new: va is not high-half: ppn={:#x}, pa={:#x}, va={:#x}, offset={:#x}",
            ppn.0,
            pa,
            va,
            crate::mm::KERNEL_OFFSET,
        );

        unsafe {
            let mut p = va as *mut u64;
            for _ in 0..(PAGE_SIZE / core::mem::size_of::<u64>()) {
                core::ptr::write_volatile(p, 0);
                p = p.add(1);
            }
        }

        Self { ppn }
    }
}

impl Drop for FrameTracker {
    fn drop(&mut self) {
        dealloc_frame(self.ppn);
    }
}

