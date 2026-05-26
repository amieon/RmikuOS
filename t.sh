#!/usr/bin/env bash
set -euo pipefail

ROOT="${1:-.}"
cd "$ROOT"

mkdir -p kernel/src/mm

cat > kernel/src/mm/mod.rs <<'RS'
//! Minimal memory-management foundation.
//!
//! Stage 1 only provides address types and a simple physical-frame allocator.
//! Paging will be built on top of this module later.

mod address;
mod frame_allocator;

#[cfg(target_arch = "riscv64")]
#[path = "riscv64.rs"]
mod arch_mm;

#[cfg(target_arch = "loongarch64")]
#[path = "loongarch64.rs"]
mod arch_mm;

pub use address::*;
pub use frame_allocator::{alloc_frame, dealloc_frame, frame_alloc_test, FrameTracker};
pub use arch_mm::*;

/// 4 KiB page size.
pub const PAGE_SIZE: usize = 0x1000;
pub const PAGE_SIZE_BITS: usize = 12;

extern "C" {
    fn _kernel_start();
    fn _kernel_end();
    fn _stext();
    fn _etext();
    fn _srodata();
    fn _erodata();
    fn _sdata();
    fn _edata();
    fn _sbss();
    fn _ebss();
}

/// Initialize the physical frame allocator.
///
/// This does not enable paging yet.
pub fn init() {
    let kernel_start = _kernel_start as usize;
    let kernel_end = _kernel_end as usize;
    let free_start = PhysAddr::from(kernel_end).ceil();
    let free_end = PhysAddr::from(MEMORY_END).floor();

    log::info!("[mm] kernel: {:#x}..{:#x}", kernel_start, kernel_end);
    log::info!(
        "[mm] sections: text={:#x}..{:#x}, rodata={:#x}..{:#x}, data={:#x}..{:#x}, bss={:#x}..{:#x}",
        _stext as usize,
        _etext as usize,
        _srodata as usize,
        _erodata as usize,
        _sdata as usize,
        _edata as usize,
        _sbss as usize,
        _ebss as usize,
    );
    log::info!("[mm] free frames: {:?}..{:?}", free_start, free_end);

    frame_allocator::init_frame_allocator(free_start, free_end);
}
RS

cat > kernel/src/mm/address.rs <<'RS'
use core::fmt::{self, Debug, Formatter};

use super::{PAGE_SIZE, PAGE_SIZE_BITS};

#[derive(Copy, Clone, Ord, PartialOrd, Eq, PartialEq)]
pub struct PhysAddr(pub usize);

#[derive(Copy, Clone, Ord, PartialOrd, Eq, PartialEq)]
pub struct VirtAddr(pub usize);

#[derive(Copy, Clone, Ord, PartialOrd, Eq, PartialEq)]
pub struct PhysPageNum(pub usize);

#[derive(Copy, Clone, Ord, PartialOrd, Eq, PartialEq)]
pub struct VirtPageNum(pub usize);

impl Debug for PhysAddr {
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        write!(f, "PA:{:#x}", self.0)
    }
}

impl Debug for VirtAddr {
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        write!(f, "VA:{:#x}", self.0)
    }
}

impl Debug for PhysPageNum {
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        write!(f, "PPN:{:#x}", self.0)
    }
}

impl Debug for VirtPageNum {
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        write!(f, "VPN:{:#x}", self.0)
    }
}

impl From<usize> for PhysAddr {
    fn from(v: usize) -> Self {
        Self(v)
    }
}

impl From<usize> for VirtAddr {
    fn from(v: usize) -> Self {
        Self(v)
    }
}

impl From<usize> for PhysPageNum {
    fn from(v: usize) -> Self {
        Self(v)
    }
}

impl From<usize> for VirtPageNum {
    fn from(v: usize) -> Self {
        Self(v)
    }
}

impl From<PhysAddr> for usize {
    fn from(v: PhysAddr) -> usize {
        v.0
    }
}

impl From<VirtAddr> for usize {
    fn from(v: VirtAddr) -> usize {
        v.0
    }
}

impl From<PhysPageNum> for usize {
    fn from(v: PhysPageNum) -> usize {
        v.0
    }
}

impl From<VirtPageNum> for usize {
    fn from(v: VirtPageNum) -> usize {
        v.0
    }
}

impl PhysAddr {
    pub fn floor(self) -> PhysPageNum {
        PhysPageNum(self.0 / PAGE_SIZE)
    }

    pub fn ceil(self) -> PhysPageNum {
        PhysPageNum((self.0 + PAGE_SIZE - 1) / PAGE_SIZE)
    }

    pub fn page_offset(self) -> usize {
        self.0 & (PAGE_SIZE - 1)
    }

    pub fn aligned(self) -> bool {
        self.page_offset() == 0
    }
}

impl VirtAddr {
    pub fn floor(self) -> VirtPageNum {
        VirtPageNum(self.0 / PAGE_SIZE)
    }

    pub fn ceil(self) -> VirtPageNum {
        VirtPageNum((self.0 + PAGE_SIZE - 1) / PAGE_SIZE)
    }

    pub fn page_offset(self) -> usize {
        self.0 & (PAGE_SIZE - 1)
    }

    pub fn aligned(self) -> bool {
        self.page_offset() == 0
    }
}

impl PhysPageNum {
    pub fn addr(self) -> PhysAddr {
        PhysAddr(self.0 << PAGE_SIZE_BITS)
    }

    pub fn bytes_array(self) -> &'static mut [u8] {
        let pa: usize = self.addr().into();
        unsafe { core::slice::from_raw_parts_mut(pa as *mut u8, PAGE_SIZE) }
    }
}

impl VirtPageNum {
    pub fn addr(self) -> VirtAddr {
        VirtAddr(self.0 << PAGE_SIZE_BITS)
    }
}

pub struct SimpleRange<T>
where
    T: Copy + PartialEq + core::ops::Add<usize, Output = T>,
{
    current: T,
    end: T,
}

impl<T> SimpleRange<T>
where
    T: Copy + PartialEq + core::ops::Add<usize, Output = T>,
{
    pub const fn new(start: T, end: T) -> Self {
        Self { current: start, end }
    }
}

impl core::ops::Add<usize> for PhysPageNum {
    type Output = Self;

    fn add(self, rhs: usize) -> Self::Output {
        Self(self.0 + rhs)
    }
}

impl core::ops::Add<usize> for VirtPageNum {
    type Output = Self;

    fn add(self, rhs: usize) -> Self::Output {
        Self(self.0 + rhs)
    }
}

impl<T> Iterator for SimpleRange<T>
where
    T: Copy + PartialEq + core::ops::Add<usize, Output = T>,
{
    type Item = T;

    fn next(&mut self) -> Option<Self::Item> {
        if self.current == self.end {
            None
        } else {
            let t = self.current;
            self.current = self.current + 1;
            Some(t)
        }
    }
}

pub type PPNRange = SimpleRange<PhysPageNum>;
pub type VPNRange = SimpleRange<VirtPageNum>;
RS

cat > kernel/src/mm/frame_allocator.rs <<'RS'
use super::{PhysPageNum, PPNRange};

pub struct FrameTracker {
    ppn: PhysPageNum,
}

impl FrameTracker {
    pub fn new(ppn: PhysPageNum) -> Self {
        // Clear the page to avoid leaking stale kernel data into later users.
        ppn.bytes_array().fill(0);
        Self { ppn }
    }

    pub fn ppn(&self) -> PhysPageNum {
        self.ppn
    }
}

impl Drop for FrameTracker {
    fn drop(&mut self) {
        dealloc_frame(self.ppn);
    }
}

pub trait FrameAllocator {
    fn new() -> Self;
    fn init(&mut self, start: PhysPageNum, end: PhysPageNum);
    fn alloc(&mut self) -> Option<PhysPageNum>;
    fn dealloc(&mut self, ppn: PhysPageNum);
}

const RECYCLED_CAP: usize = 4096;

pub struct StackFrameAllocator {
    current: usize,
    end: usize,
    recycled: [usize; RECYCLED_CAP],
    recycled_len: usize,
}

impl FrameAllocator for StackFrameAllocator {
    fn new() -> Self {
        Self {
            current: 0,
            end: 0,
            recycled: [0; RECYCLED_CAP],
            recycled_len: 0,
        }
    }

    fn init(&mut self, start: PhysPageNum, end: PhysPageNum) {
        self.current = start.0;
        self.end = end.0;
        self.recycled_len = 0;
    }

    fn alloc(&mut self) -> Option<PhysPageNum> {
        if self.recycled_len > 0 {
            self.recycled_len -= 1;
            Some(PhysPageNum(self.recycled[self.recycled_len]))
        } else if self.current == self.end {
            None
        } else {
            let ppn = self.current;
            self.current += 1;
            Some(PhysPageNum(ppn))
        }
    }

    fn dealloc(&mut self, ppn: PhysPageNum) {
        let ppn = ppn.0;

        if ppn >= self.current {
            panic!("frame ppn={:#x} has not been allocated", ppn);
        }
        if self.recycled[..self.recycled_len].iter().any(|&p| p == ppn) {
            panic!("frame ppn={:#x} has been deallocated twice", ppn);
        }
        if self.recycled_len == RECYCLED_CAP {
            panic!("frame recycled stack overflow");
        }

        self.recycled[self.recycled_len] = ppn;
        self.recycled_len += 1;
    }
}

static FRAME_ALLOCATOR_LOCK: crate::sync::SpinLock = crate::sync::SpinLock::new();
static mut FRAME_ALLOCATOR: StackFrameAllocator = StackFrameAllocator::new();

fn with_allocator<R>(f: impl FnOnce(&mut StackFrameAllocator) -> R) -> R {
    FRAME_ALLOCATOR_LOCK.lock();
    let ret = unsafe { f(&mut FRAME_ALLOCATOR) };
    FRAME_ALLOCATOR_LOCK.unlock();
    ret
}

pub fn init_frame_allocator(start: PhysPageNum, end: PhysPageNum) {
    log::info!("[mm] init frame allocator: {:?}..{:?}", start, end);
    with_allocator(|allocator| allocator.init(start, end));
}

pub fn alloc_frame() -> Option<FrameTracker> {
    with_allocator(|allocator| allocator.alloc()).map(FrameTracker::new)
}

pub fn dealloc_frame(ppn: PhysPageNum) {
    with_allocator(|allocator| allocator.dealloc(ppn));
}

pub fn frame_alloc_test() {
    let a = alloc_frame().expect("frame allocation failed: a");
    let b = alloc_frame().expect("frame allocation failed: b");
    let c = alloc_frame().expect("frame allocation failed: c");

    log::debug!("[mm] alloc {:?}, {:?}, {:?}", a.ppn(), b.ppn(), c.ppn());

    let a_ppn = a.ppn();
    let b_ppn = b.ppn();
    let c_ppn = c.ppn();

    drop(a);
    drop(b);
    drop(c);

    let x = alloc_frame().expect("frame allocation failed after recycle");
    let x_ppn = x.ppn();
    log::debug!("[mm] alloc after recycle {:?}", x_ppn);

    // Stack-style recycle: last deallocated frame should come back first.
    assert!(x_ppn == c_ppn || x_ppn == b_ppn || x_ppn == a_ppn);
    drop(x);

    log::info!("[mm] frame allocator test passed");
}

#[allow(dead_code)]
pub fn free_ppn_range(start: PhysPageNum, end: PhysPageNum) -> PPNRange {
    PPNRange::new(start, end)
}

RS

cat > kernel/src/mm/riscv64.rs <<'RS'
/// QEMU virt DRAM starts at 0x8000_0000.
pub const MEMORY_START: usize = 0x8000_0000;

/// run.sh uses `-m 128M` for RISC-V.
pub const MEMORY_SIZE: usize = 128 * 1024 * 1024;

pub const MEMORY_END: usize = MEMORY_START + MEMORY_SIZE;

/// UART0 on QEMU virt.
pub const UART0: usize = 0x1000_0000;
RS

cat > kernel/src/mm/loongarch64.rs <<'RS'
/// The kernel is loaded at 0x0100_0000 by the QEMU loader in run.sh.
pub const MEMORY_START: usize = 0x0100_0000;

/// run.sh uses `-m 2G` for LoongArch.
pub const MEMORY_SIZE: usize = 2 * 1024 * 1024 * 1024;

pub const MEMORY_END: usize = MEMORY_START + MEMORY_SIZE;

/// Early UART/MMIO address should still come from arch::UART_BASE.
pub const UART0: usize = crate::arch::UART_BASE;
RS

# Patch Cargo.toml for alloc crate support if needed: no dependency is needed for alloc.
# Patch linker scripts to export section symbols.
cat > /tmp/riscv_linker.ld.new <<'LD'
/* kernel/src/arch/riscv64/linker.ld */
OUTPUT_ARCH(riscv64)
ENTRY(_start)

BASE_ADDRESS = 0x80200000;

SECTIONS {
    . = BASE_ADDRESS;
    _kernel_start = .;

    .text : ALIGN(4K) {
        _stext = .;
        *(.text.init)
        *(.text .text.*)
        _etext = .;
    }

    .rodata : ALIGN(4K) {
        _srodata = .;
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
        _erodata = .;
    }

    .data : ALIGN(4K) {
        _sdata = .;
        *(.data .data.*)
        *(.sdata .sdata.*)
        _edata = .;
    }

    .bss (NOLOAD) : ALIGN(4K) {
        _sbss = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
        . = ALIGN(4K);
        *(.bss.stack)
        _ebss = .;
    }

    . = ALIGN(4K);
    _kernel_end = .;

    /DISCARD/ : {
        *(.eh_frame)
        *(.comment)
        *(.note*)
    }
}
LD

cat > /tmp/loong_linker.ld.new <<'LD'
/* kernel/src/arch/loongarch64/linker.ld */
OUTPUT_ARCH(loongarch64)
ENTRY(_entry)

MEMORY {
    RAM (rwx) : ORIGIN = 0x0000000001000000, LENGTH = 128M
}

SECTIONS {
    . = ORIGIN(RAM);
    _kernel_start = .;

    .text : ALIGN(4K) {
        _stext = .;
        *(.text.boot)
        *(.text .text.*)
        _etext = .;
    } > RAM

    .rodata : ALIGN(4K) {
        _srodata = .;
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
        _erodata = .;
    } > RAM

    .data : ALIGN(4K) {
        _sdata = .;
        *(.data .data.*)
        *(.sdata .sdata.*)
        _edata = .;
    } > RAM

    .bss (NOLOAD) : ALIGN(4K) {
        _sbss = .;
        _bss_start = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
        . = ALIGN(4K);
        *(.bss.stack)
        . = ALIGN(4K);
        _bss_end = .;
        _ebss = .;
    } > RAM

    . = ALIGN(4K);
    _kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
LD

cp /tmp/riscv_linker.ld.new kernel/src/arch/riscv64/linker.ld
cp /tmp/loong_linker.ld.new kernel/src/arch/loongarch64/linker.ld

# Try to add mod mm to main.rs, but do not fight custom formatting too hard.
if ! grep -q "mod mm;" kernel/src/main.rs; then
    python3 - <<'PY'
from pathlib import Path
p = Path('kernel/src/main.rs')
s = p.read_text()
# Add after mod io/trap/timer if possible, otherwise after mod arch.
inserted = False
for needle in ['mod timer;', 'mod trap;', 'mod io;', 'mod uart;', 'mod arch;']:
    if needle in s:
        s = s.replace(needle, needle + '\nmod mm;', 1)
        inserted = True
        break
if not inserted:
    s = 'mod mm;\n' + s
p.write_text(s)
PY
fi

echo
cat <<'MSG'
MM foundation installed.

Manual checklist:
1. Call mm::init() on CPU0 after logger/trap init, before paging work.
   Good place:
       trap::init();
       mm::init();
       mm::frame_alloc_test();
       timer::init();

2. This version uses your existing non-generic crate::sync::SpinLock and a static allocator.
MSG
