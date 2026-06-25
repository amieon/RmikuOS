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

// ========== 高级命令行输入 ==========

pub const ESC: u8 = 0x1B;
const HISTORY_MAX: usize = 10;
const LINE_MAX: usize = 64;

/// 高级命令行读取：支持历史、左右移动、退格
/// 返回输入的字节数组（以 0 结尾）
pub fn readline_cmd(_prompt: &str) -> [u8; LINE_MAX] {
    // 提示符由调用者打印，这里只负责输入

    let mut buf = [0u8; LINE_MAX];
    let mut len = 0usize;
    let mut cursor = 0usize;
    let mut history: [[u8; LINE_MAX]; HISTORY_MAX] = [[0; LINE_MAX]; HISTORY_MAX];
    let mut hist_count = 0usize;
    let mut hist_pos = 0usize;

    loop {
        let mut ch = 0u8;
        let n = read(0, core::slice::from_mut(&mut ch));
        if n <= 0 { continue; }

        match ch {
            b'\n' | b'\r' => {
                // 回车：换行并返回
                put_char(b'\n');
                if len > 0 && hist_count < HISTORY_MAX {
                    let copy_len = if len < LINE_MAX { len } else { LINE_MAX - 1 };
                    history[hist_count][..copy_len].copy_from_slice(&buf[..copy_len]);
                    history[hist_count][copy_len] = 0;
                    hist_count += 1;
                }
                return buf;
            }

            8 | 127 => {
                // 退格
                if cursor > 0 {
                    put_char(8);
                    for i in cursor..len {
                        buf[i - 1] = buf[i];
                    }
                    len -= 1;
                    cursor -= 1;
                    for i in cursor..len {
                        put_char(buf[i]);
                    }
                    put_char(b' ');
                    for _ in cursor..len {
                        put_char(8);
                    }
                }
            }

            ESC => {
                let mut seq = [0u8; 2];
                if read(0, &mut seq[..1]) <= 0 { continue; }
                if seq[0] == b'[' {
                    if read(0, &mut seq[1..2]) <= 0 { continue; }
                    match seq[1] {
                        b'A' => {
                            // 上键
                            if hist_pos < hist_count {
                                hist_pos += 1;
                                // 清除当前行
                                for _ in 0..len { put_char(8); }
                                for _ in 0..len { put_char(b' '); }
                                for _ in 0..len { put_char(8); }
                                let hist_line = &history[hist_count - hist_pos];
                                let hist_len = bytes_len(hist_line);
                                len = hist_len;
                                cursor = len;
                                for i in 0..len {
                                    buf[i] = hist_line[i];
                                    put_char(hist_line[i]);
                                }
                            }
                        }
                        b'B' => {
                            // 下键
                            if hist_pos > 0 {
                                hist_pos -= 1;
                                for _ in 0..len { put_char(8); }
                                for _ in 0..len { put_char(b' '); }
                                for _ in 0..len { put_char(8); }
                                if hist_pos == 0 {
                                    len = 0;
                                    cursor = 0;
                                } else {
                                    let hist_line = &history[hist_count - hist_pos];
                                    let hist_len = bytes_len(hist_line);
                                    len = hist_len;
                                    cursor = len;
                                    for i in 0..len {
                                        buf[i] = hist_line[i];
                                        put_char(hist_line[i]);
                                    }
                                }
                            }
                        }
                        b'C' => {
                            // 右键
                            if cursor < len {
                                put_char(b'\x1B');
                                put_char(b'[');
                                put_char(b'C');
                                cursor += 1;
                            }
                        }
                        b'D' => {
                            // 左键
                            if cursor > 0 {
                                put_char(b'\x1B');
                                put_char(b'[');
                                put_char(b'D');
                                cursor -= 1;
                            }
                        }
                        _ => {}
                    }
                }
            }

            _ if ch >= 32 && ch < 127 => {
                // 普通字符插入
                if len < LINE_MAX - 1 {
                    for i in (cursor..len).rev() {
                        buf[i + 1] = buf[i];
                    }
                    buf[cursor] = ch;
                    len += 1;
                    cursor += 1;
                    for i in (cursor - 1)..len {
                        put_char(buf[i]);
                    }
                    for _ in cursor..len {
                        put_char(8);
                    }
                }
            }

            _ => {}
        }
    }
}

/// 获取字节数组中字符串长度（遇到 0 停止）
fn bytes_len(b: &[u8]) -> usize {
    b.iter().position(|&c| c == 0).unwrap_or(b.len())
}

// ========== 键盘输入解析 ==========

use ulib::io::{puts};   // 添加 puts

// ... 原有 put_int, readline, atoi, bytes_to_str, copy_str_to_buf 保持不变 ...

// ========== 键盘输入解析 ==========

#[derive(Debug, PartialEq)]
pub enum Key {
    Up,
    Down,
    Enter,
    W,
    S,
    Other,
}

pub fn get_key() -> Key {
    let mut ch = 0u8;
    if read(0, core::slice::from_mut(&mut ch)) <= 0 {
        return Key::Other;
    }
    if ch == b'\n' || ch == b'\r' {
        return Key::Enter;
    }
    if ch == b'w' || ch == b'W' {
        return Key::W;
    }
    if ch == b's' || ch == b'S' {
        return Key::S;
    }
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
    if n == 0 {
        put_char(b'0');
        return;
    }
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