use ulib::io::{read, put_char, puts};

// ---------- 基础 I/O ----------
pub fn put_int(mut x: u64) {
    if x == 0 { put_char(b'0'); return; }
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

pub fn readline(buf: &mut [u8]) -> usize {
    let mut i = 0;
    while i < buf.len() - 1 {
        let mut ch = 0u8;
        let n = read(0, core::slice::from_mut(&mut ch));
        if n <= 0 { continue; }
        if ch == b'\r' { ch = b'\n'; }
        if ch == b'\n' { put_char(b'\n'); break; }
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

pub fn atoi(bytes: &[u8]) -> u32 {
    let mut val = 0u32;
    for &c in bytes {
        if c < b'0' || c > b'9' { break; }
        val = val * 10 + (c - b'0') as u32;
    }
    val
}

pub fn bytes_to_str(b: &[u8]) -> &str {
    let len = b.iter().position(|&c| c == 0).unwrap_or(b.len());
    core::str::from_utf8(&b[..len]).unwrap_or("")
}

pub fn copy_str_to_buf(dst: &mut [u8], src: &str) {
    let bytes = src.as_bytes();
    let len = if bytes.len() < dst.len() { bytes.len() } else { dst.len() - 1 };
    dst[..len].copy_from_slice(&bytes[..len]);
    dst[len] = 0;
}

// ---------- 菜单 UI 核心 ----------
#[derive(Debug, PartialEq)]
pub enum Key {
    Up, Down, Enter, W, S, Other,
}

pub fn get_key() -> Key {
    let mut ch = 0u8;
    if read(0, core::slice::from_mut(&mut ch)) <= 0 { return Key::Other; }
    if ch == b'\n' || ch == b'\r' { return Key::Enter; }
    if ch == b'w' || ch == b'W' { return Key::W; }
    if ch == b's' || ch == b'S' { return Key::S; }
    if ch == 0x1B {
        let mut seq = [0u8; 2];
        if read(0, &mut seq[..1]) <= 0 { return Key::Other; }
        if seq[0] == b'[' {
            if read(0, &mut seq[1..2]) <= 0 { return Key::Other; }
            match seq[1] {
                b'A' => return Key::Up,
                b'B' => return Key::Down,
                _ => {}
            }
        }
    }
    Key::Other
}

pub fn wait_any_key() {
    let _ = get_key();
}

pub fn print_at(row: u16, col: u16, s: &str) {
    put_char(0x1B);
    put_char(b'[');
    print_num(row);
    put_char(b';');
    print_num(col);
    put_char(b'H');
    puts(s);
}

fn print_num(mut n: u16) {
    if n == 0 { put_char(b'0'); return; }
    let mut buf = [0u8; 5];
    let mut i = 0;
    while n > 0 {
        buf[i] = b'0' + (n % 10) as u8;
        n /= 10;
        i += 1;
    }
    while i > 0 {
        i -= 1;
        put_char(buf[i]);
    }
}