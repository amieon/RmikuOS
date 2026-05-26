use core::fmt::{self, Debug, Formatter};

use super::config::{PAGE_SIZE, PAGE_SIZE_BITS};

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
