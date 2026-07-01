//! 按键解析:从 stdin 读字节,翻译成 Key 枚举。
//!
//! 注意:你的 read 是阻塞的(没数据会自旋等待)。方向键是 ESC 序列
//! (ESC '[' 'A'/'B'/'C'/'D' 三个字节连发),由于它们瞬间连续到达,
//! 连读三个字节通常不会卡住。单独按 ESC 会停在第二次 read 等待——
//! 教学场景可接受(用户一般用 Ctrl+Q 退出而非裸 ESC)。

use ulib::io::read;

#[derive(Clone, Copy, PartialEq)]
pub enum Key {
    Up,
    Down,
    Left,
    Right,
    Char(u8),
    Enter,
    Backspace,
    CtrlS,
    CtrlQ,
    Other,
}

/// 读一个字节(阻塞)。返回 None 表示读失败。
fn read_byte() -> Option<u8> {
    let mut ch = 0u8;
    let n = read(0, core::slice::from_mut(&mut ch));
    if n <= 0 {
        None
    } else {
        Some(ch)
    }
}

/// 读并解析一个按键
pub fn read_key() -> Key {
    let ch = match read_byte() {
        Some(c) => c,
        None => return Key::Other,
    };

    // ESC 序列:可能是方向键 ESC [ A/B/C/D
    if ch == 0x1b {
        let b1 = match read_byte() {
            Some(c) => c,
            None => return Key::Other,
        };
        if b1 == b'[' {
            let b2 = match read_byte() {
                Some(c) => c,
                None => return Key::Other,
            };
            return match b2 {
                b'A' => Key::Up,
                b'B' => Key::Down,
                b'C' => Key::Right,
                b'D' => Key::Left,
                _ => Key::Other,
            };
        }
        return Key::Other;
    }

    if ch == b'\r' || ch == b'\n' {
        return Key::Enter;
    }
    if ch == 8 || ch == 127 {
        return Key::Backspace;
    }
    if ch == 0x13 {
        return Key::CtrlS; // Ctrl+S
    }
    if ch == 0x11 {
        return Key::CtrlQ; // Ctrl+Q
    }
    if ch >= 32 && ch < 127 {
        return Key::Char(ch);
    }

    Key::Other
}
