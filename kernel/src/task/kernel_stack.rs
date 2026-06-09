use crate::mm::{
    kernel_phys_to_virt,
    PhysPageNum,
    PAGE_SIZE_BITS,
};
use crate::mm::config::PAGE_SIZE;
use crate::mm::frame_allocator::{
    alloc_contiguous_frames,
    dealloc_contiguous_frames,
};
use crate::trap::TrapContext;

pub const KERNEL_STACK_SIZE: usize = 128 * 1024;
const TRAP_CONTEXT_SIZE: usize = core::mem::size_of::<TrapContext>();

pub struct KernelStack {
    base_ppn: PhysPageNum,
    pages: usize,
}

const KERNEL_STACK_MAGIC: usize = 0xdead_beef_cafe_babe;


impl KernelStack {
    pub fn new() -> Self {
        assert!(
            KERNEL_STACK_SIZE % PAGE_SIZE == 0,
            "KERNEL_STACK_SIZE must be page aligned"
        );

        let pages = KERNEL_STACK_SIZE / PAGE_SIZE;

        let base_ppn = alloc_contiguous_frames(pages)
            .expect("failed to allocate kernel stack frames");

        let base_pa = base_ppn.0 << PAGE_SIZE_BITS;
        let base_va = kernel_phys_to_virt(base_pa);

        assert!(
            base_va >= crate::mm::config::KERNEL_OFFSET,
            "[kstack] bad kernel stack va: ppn={:#x}, pa={:#x}, va={:#x}",
            base_ppn.0,
            base_pa,
            base_va,
        );

        unsafe {
            core::ptr::write_bytes(
                base_va as *mut u8,
                0,
                KERNEL_STACK_SIZE,
            );
        }

        let stack = Self {
            base_ppn,
            pages,
        };

        stack.init_guard();

        stack
    }

    pub fn bottom(&self) -> usize {
        kernel_phys_to_virt(self.base_ppn.0 << PAGE_SIZE_BITS)
    }

    pub fn top(&self) -> usize {
        self.bottom() + self.pages * PAGE_SIZE
    }

    fn init_guard(&self) {
        unsafe {
            let magic_ptr = self.bottom() as *mut usize;
            magic_ptr.write_volatile(KERNEL_STACK_MAGIC);
        }
    }

    pub fn check_guard(&self) {
        let magic = unsafe {
            (self.bottom() as *const usize).read_volatile()
        };

        assert_eq!(
            magic,
            KERNEL_STACK_MAGIC,
            "kernel stack overflow detected"
        );
    }


    pub unsafe fn push_context(&self, cx: TrapContext) -> *mut TrapContext {
        let actual_size = core::mem::size_of::<TrapContext>();

        assert_eq!(
            TRAP_CONTEXT_SIZE,
            actual_size,
            "TRAP_CONTEXT_SIZE mismatch"
        );

        assert!(
            actual_size <= KERNEL_STACK_SIZE,
            "TrapContext too large"
        );

        let cx_ptr = (self.top() - actual_size) as *mut TrapContext;
        cx_ptr.write(cx);
        cx_ptr
    }
}

impl Drop for KernelStack {
    fn drop(&mut self) {
        dealloc_contiguous_frames(self.base_ppn, self.pages);
    }
}