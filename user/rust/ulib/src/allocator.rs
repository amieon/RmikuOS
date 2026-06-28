// ulib/src/allocator.rs
use core::alloc::{GlobalAlloc, Layout};
use core::ptr::null_mut;
use core::cell::UnsafeCell;

use crate::syscall::syscall3;
use crate::number::SYS_MMAP;   

const PROT_READ:  usize = 1;
const PROT_WRITE: usize = 2;

const PAGE_SIZE:  usize = 4096;
const CHUNK_SIZE: usize = 64 * 1024;   

fn mmap(len: usize) -> usize {
    let ret = unsafe {
        syscall3(SYS_MMAP, len, PROT_READ | PROT_WRITE, 0)
    };
    if ret < 0 {
        0
    } else {
        ret as usize
    }
}


struct Bump {
    inner: UnsafeCell<BumpInner>,
}

struct BumpInner {
    next: usize,
    end:  usize,
}


unsafe impl Sync for Bump {}

impl Bump {
    const fn new() -> Self {
        Self {
            inner: UnsafeCell::new(BumpInner { next: 0, end: 0 }),
        }
    }
}

unsafe impl GlobalAlloc for Bump {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        let inner = &mut *self.inner.get();

        let size  = layout.size();
        let align = layout.align();

        let aligned = (inner.next + align - 1) & !(align - 1);

        if aligned + size <= inner.end {
            inner.next = aligned + size;
            return aligned as *mut u8;
        }


        let need = size + align;
        let mut chunk = if need > CHUNK_SIZE { need } else { CHUNK_SIZE };
        chunk = (chunk + PAGE_SIZE - 1) & !(PAGE_SIZE - 1); 

        let base = mmap(chunk);
        if base == 0 {
            return null_mut();
        }

        inner.next = base;
        inner.end  = base + chunk;


        let aligned = (inner.next + align - 1) & !(align - 1);
        if aligned + size <= inner.end {
            inner.next = aligned + size;
            aligned as *mut u8
        } else {
            null_mut()
        }
    }

    unsafe fn dealloc(&self, _ptr: *mut u8, _layout: Layout) {
        // bump 不回收
    }
}

#[global_allocator]
static ALLOCATOR: Bump = Bump::new();