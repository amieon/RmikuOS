//! 终端控制:把 ANSI 转义序列拼进一个字节缓冲(帧),最后一次性 write 出去。
//!
//! 关键设计:不直接往屏幕一次次 write(那样慢且闪),而是把一整帧画面
//! (光标定位 + 文本 + 清行尾...)拼进一个 Vec<u8>,最后调一次 write。
//! 这样终端一次收到完整的一帧,既快又不闪。

extern crate alloc;
use alloc::vec::Vec;

use ulib::io::write;

/// 一帧的字节缓冲。所有绘制操作往这里追加,最后 flush 一次写出。
pub struct Frame {
    buf: Vec<u8>,
}

impl Frame {
    pub fn new() -> Self {
        Frame { buf: Vec::new() }
    }

    /// 清空帧缓冲(开始画新一帧前调用)
    pub fn clear(&mut self) {
        self.buf.clear();
    }

    /// 追加原始字节
    pub fn put_bytes(&mut self, bytes: &[u8]) {
        self.buf.extend_from_slice(bytes);
    }

    /// 追加一个字节
    pub fn put_byte(&mut self, b: u8) {
        self.buf.push(b);
    }

    /// 追加一个 &str
    pub fn put_str(&mut self, s: &str) {
        self.buf.extend_from_slice(s.as_bytes());
    }

    /// 追加一个十进制数字(用于 ANSI 序列里的行列号)
    pub fn put_num(&mut self, mut n: u32) {
        if n == 0 {
            self.buf.push(b'0');
            return;
        }
        // 先收集各位数字(逆序),再正序写入
        let mut tmp = [0u8; 10];
        let mut i = 0;
        while n > 0 {
            tmp[i] = b'0' + (n % 10) as u8;
            n /= 10;
            i += 1;
        }
        while i > 0 {
            i -= 1;
            self.buf.push(tmp[i]);
        }
    }

    // ---------- ANSI 控制序列 ----------

    /// 光标移到 (row, col),都从 1 开始。ESC [ row ; col H
    pub fn move_to(&mut self, row: u32, col: u32) {
        self.put_bytes(b"\x1b[");
        self.put_num(row);
        self.put_byte(b';');
        self.put_num(col);
        self.put_byte(b'H');
    }

    /// 光标回到左上角 (1,1)。ESC [ H
    pub fn home(&mut self) {
        self.put_bytes(b"\x1b[H");
    }

    /// 清除从光标到行尾。ESC [ K
    pub fn clear_to_eol(&mut self) {
        self.put_bytes(b"\x1b[K");
    }

    /// 隐藏光标。ESC [ ? 25 l
    pub fn hide_cursor(&mut self) {
        self.put_bytes(b"\x1b[?25l");
    }

    /// 显示光标。ESC [ ? 25 h
    pub fn show_cursor(&mut self) {
        self.put_bytes(b"\x1b[?25h");
    }

    /// 清整个屏幕(只在进入/退出编辑器时用一次,不在每帧用)
    pub fn clear_screen(&mut self) {
        self.put_bytes(b"\x1b[2J\x1b[H");
    }

    /// 把整帧一次性写到 stdout
    pub fn flush(&mut self) {
        if !self.buf.is_empty() {
            write(1, &self.buf);
        }
    }
}
