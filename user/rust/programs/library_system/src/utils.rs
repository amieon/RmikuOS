use ulib::io::{read, put_char};

/// 打印无符号 64 位整数（不换行）
pub fn put_int(mut x: u64) {
    if x == 0 {
        put_char(b'0');
        return;
    }
    let mut buf = [0u8; 20];
    let mut i = 0;
    while x > 0 {
        buf[i] = b'0' + (x % 10) as u8;
        x /= 10;
        i += 1;
    }
    while i > 0 {
        i -= 1;
        put_char(buf[i]);
    }
}

/// 读取一行（支持退格），返回长度
pub fn readline(buf: &mut [u8]) -> usize {
    let mut i = 0;
    while i < buf.len() - 1 {
        let mut ch = 0u8;
        let n = read(0, core::slice::from_mut(&mut ch));
        if n <= 0 { continue; }
        if ch == b'\r' { ch = b'\n'; }
        if ch == b'\n' {
            put_char(b'\n');
            break;
        }
        if ch == 8 || ch == 127 {
            if i > 0 {
                i -= 1;
                put_char(8);
                put_char(b' ');
                put_char(8);
            }
            continue;
        }
        buf[i] = ch;
        i += 1;
        put_char(ch);
    }
    buf[i] = 0;
    i
}

/// 字节数组转整数（遇到非数字停止）
pub fn atoi(bytes: &[u8]) -> u32 {
    let mut val = 0u32;
    for &c in bytes {
        if c < b'0' || c > b'9' { break; }
        val = val * 10 + (c - b'0') as u32;
    }
    val
}

/// 从字节数组中提取字符串（遇到 0 截断）
pub fn bytes_to_str(b: &[u8]) -> &str {
    let len = b.iter().position(|&c| c == 0).unwrap_or(b.len());
    core::str::from_utf8(&b[..len]).unwrap_or("")
}

/// 将字符串复制到固定长度字节数组（末尾补0）
pub fn copy_str_to_buf(dst: &mut [u8], src: &str) {
    let bytes = src.as_bytes();
    let len = if bytes.len() < dst.len() { bytes.len() } else { dst.len() - 1 };
    dst[..len].copy_from_slice(&bytes[..len]);
    dst[len] = 0;
}