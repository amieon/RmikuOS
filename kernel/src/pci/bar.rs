use super::ecam::{
    PciAddress,
    read_config_u32,
    write_config_u32,
};

pub fn read_bar_raw(addr: PciAddress, bar: u8) -> u32 {
    assert!(bar < 6);
    read_config_u32(addr, 0x10 + (bar as usize) * 4)
}

pub fn read_bar(addr: PciAddress, bar: u8) -> u64 {
    assert!(bar < 6);

    let off = 0x10 + (bar as usize) * 4;
    let lo = read_config_u32(addr, off);

    if lo & 0x1 != 0 {
        return (lo & !0x3) as u64;
    }

    let bar_type = (lo >> 1) & 0x3;

    if bar_type == 0x2 {
        let hi = read_config_u32(addr, off + 4);
        ((hi as u64) << 32) | ((lo & !0xf) as u64)
    } else {
        (lo & !0xf) as u64
    }
}

pub fn assign_mem_bar(addr: PciAddress, bar: u8, base: usize) {
    assert!(bar < 6);
    assert!(base & 0xf == 0);

    let off = 0x10 + (bar as usize) * 4;

    let old_lo = read_config_u32(addr, off);

    if old_lo & 0x1 != 0 {
        panic!("[pci] BAR{} is I/O BAR, not memory BAR", bar);
    }

    let flags = old_lo & 0xf;
    let bar_type = (old_lo >> 1) & 0x3;

    let new_lo = ((base as u32) & !0xf) | flags;

    write_config_u32(addr, off, new_lo);

    if bar_type == 0x2 {
        assert!(bar + 1 < 6);

        let new_hi = (base as u64 >> 32) as u32;
        write_config_u32(addr, off + 4, new_hi);
    }

    let new_base = read_bar(addr, bar);

    log::info!(
        "[pci] assign BAR{}: old_raw={:#x}, type={}, base={:#x}->{:#x}",
        bar,
        old_lo,
        bar_type,
        0usize,
        new_base,
    );
}

pub fn ensure_mem_bar(addr: PciAddress, bar: u8, base: usize) {
    let old_base = read_bar(addr, bar);

    if old_base != 0 {
        log::info!(
            "[pci] BAR{} already assigned: base={:#x}",
            bar,
            old_base,
        );
        return;
    }

    assign_mem_bar(addr, bar, base);
}