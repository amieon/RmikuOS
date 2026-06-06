use core::mem::size_of;

pub const ELF_MAGIC: &[u8; 4] = b"\x7fELF";

pub const ELF_CLASS_64: u8 = 2;
pub const ELF_DATA_LSB: u8 = 1;
pub const ELF_VERSION_CURRENT: u8 = 1;

pub const ET_EXEC: u16 = 2;
pub const ET_DYN: u16 = 3;

pub const PT_LOAD: u32 = 1;
pub const PT_INTERP: u32 = 3;

pub const PF_X: u32 = 1;
pub const PF_W: u32 = 2;
pub const PF_R: u32 = 4;

#[cfg(target_arch = "riscv64")]
pub const ELF_MACHINE: u16 = 243; // EM_RISCV

#[cfg(target_arch = "loongarch64")]
pub const ELF_MACHINE: u16 = 258; // EM_LOONGARCH

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Elf64Header {
    pub ident: [u8; 16],
    pub e_type: u16,
    pub e_machine: u16,
    pub e_version: u32,
    pub e_entry: u64,
    pub e_phoff: u64,
    pub e_shoff: u64,
    pub e_flags: u32,
    pub e_ehsize: u16,
    pub e_phentsize: u16,
    pub e_phnum: u16,
    pub e_shentsize: u16,
    pub e_shnum: u16,
    pub e_shstrndx: u16,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Elf64ProgramHeader {
    pub p_type: u32,
    pub p_flags: u32,
    pub p_offset: u64,
    pub p_vaddr: u64,
    pub p_paddr: u64,
    pub p_filesz: u64,
    pub p_memsz: u64,
    pub p_align: u64,
}

fn read_unaligned<T: Copy>(data: &[u8], offset: usize) -> Option<T> {
    if offset.checked_add(size_of::<T>())? > data.len() {
        return None;
    }

    Some(unsafe {
        core::ptr::read_unaligned(data.as_ptr().add(offset) as *const T)
    })
}

impl Elf64Header {
    pub fn parse(data: &[u8]) -> Option<Self> {
        let header: Self = read_unaligned(data, 0)?;

        if &header.ident[0..4] != ELF_MAGIC {
            return None;
        }

        if header.ident[4] != ELF_CLASS_64 {
            return None;
        }

        if header.ident[5] != ELF_DATA_LSB {
            return None;
        }

        if header.ident[6] != ELF_VERSION_CURRENT {
            return None;
        }

        if header.e_machine != ELF_MACHINE {
            return None;
        }

        if header.e_version != 1 {
            return None;
        }


        if header.e_type != ET_EXEC {
            return None;
        }

        if header.e_type != ET_EXEC {
            return None;
        }

        if header.e_phentsize as usize != size_of::<Elf64ProgramHeader>() {
            return None;
        }

        Some(header)
    }

    pub fn program_header(&self, data: &[u8], index: usize) -> Option<Elf64ProgramHeader> {
        if index >= self.e_phnum as usize {
            return None;
        }

        let offset = (self.e_phoff as usize)
            .checked_add(index.checked_mul(self.e_phentsize as usize)?)?;

        read_unaligned(data, offset)
    }
}