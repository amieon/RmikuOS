use super::ecam::{
    PciAddress,
    read_config_u32,
};

pub fn read_bar(addr: PciAddress, bar: u8) -> u64 {
    assert!(bar < 6);

    let off = 0x10 + (bar as usize) * 4;
    let lo = read_config_u32(addr, off);

    if lo & 0x1 != 0 {
        /*
         * I/O BAR，第一版先不支持。
         */
        return (lo & !0x3) as u64;
    }

    let bar_type = (lo >> 1) & 0x3;

    if bar_type == 0x2 {
        /*
         * 64-bit memory BAR。
         */
        assert!(bar + 1 < 6);

        let hi = read_config_u32(addr, off + 4);

        (((hi as u64) << 32) | ((lo & !0xf) as u64))
    } else {
        /*
         * 32-bit memory BAR。
         */
        (lo & !0xf) as u64
    }
}