//! 文本缓冲:用 Vec<u8> 存全部文本,一个字节偏移 cursor 表示光标位置。
//!
//! 这一层是纯逻辑,完全不碰屏幕/ANSI,所以容易推理和测试。
//! 渲染层(render.rs)只读这里的数据,不修改。

extern crate alloc;
use alloc::vec::Vec;

pub struct Buffer {
    /// 全部文本内容
    pub data: Vec<u8>,
    /// 光标在 data 中的字节偏移(0..=data.len())
    pub cursor: usize,
}

impl Buffer {
    pub fn new() -> Self {
        Buffer {
            data: Vec::new(),
            cursor: 0,
        }
    }

    pub fn len(&self) -> usize {
        self.data.len()
    }

    /// 用文件内容初始化
    pub fn load(&mut self, content: &[u8]) {
        self.data.clear();
        self.data.extend_from_slice(content);
        self.cursor = 0;
    }

    // ---------- 编辑操作 ----------

    /// 在光标处插入一个字节,光标后移
    pub fn insert(&mut self, ch: u8) {
        if self.cursor > self.data.len() {
            self.cursor = self.data.len();
        }
        self.data.insert(self.cursor, ch);
        self.cursor += 1;
    }

    /// 删除光标前一个字节(退格)
    pub fn backspace(&mut self) {
        if self.cursor == 0 || self.cursor > self.data.len() {
            return;
        }
        self.data.remove(self.cursor - 1);
        self.cursor -= 1;
    }

    // ---------- 光标行列计算 ----------

    /// 当前光标所在的 (行, 列),都从 0 开始。
    /// 行 = 光标前有几个 '\n';列 = 光标到本行行首的距离。
    pub fn cursor_line_col(&self) -> (usize, usize) {
        let mut line = 0;
        let mut col = 0;
        let mut i = 0;
        while i < self.cursor && i < self.data.len() {
            if self.data[i] == b'\n' {
                line += 1;
                col = 0;
            } else {
                col += 1;
            }
            i += 1;
        }
        (line, col)
    }

    /// 第 n 行(0 起)的起始字节偏移。如果行号超出,返回 data.len()。
    pub fn line_start_offset(&self, target_line: usize) -> usize {
        if target_line == 0 {
            return 0;
        }
        let mut line = 0;
        for i in 0..self.data.len() {
            if self.data[i] == b'\n' {
                line += 1;
                if line == target_line {
                    return i + 1; // 换行符的下一个位置是新行起点
                }
            }
        }
        self.data.len()
    }

    /// 第 n 行(0 起)的长度(不含换行符)
    pub fn line_len(&self, target_line: usize) -> usize {
        let start = self.line_start_offset(target_line);
        let mut len = 0;
        let mut i = start;
        while i < self.data.len() && self.data[i] != b'\n' {
            len += 1;
            i += 1;
        }
        len
    }

    /// 总行数(至少 1)
    pub fn line_count(&self) -> usize {
        let mut lines = 1;
        for &b in self.data.iter() {
            if b == b'\n' {
                lines += 1;
            }
        }
        lines
    }

    // ---------- 光标移动 ----------

    pub fn move_left(&mut self) {
        if self.cursor > 0 {
            self.cursor -= 1;
        }
    }

    pub fn move_right(&mut self) {
        if self.cursor < self.data.len() {
            self.cursor += 1;
        }
    }

    /// 上移一行,尽量保持列。
    pub fn move_up(&mut self) {
        let (line, col) = self.cursor_line_col();
        if line == 0 {
            return;
        }
        let prev_line = line - 1;
        let prev_len = self.line_len(prev_line);
        let new_col = if col < prev_len { col } else { prev_len }; // 上一行更短就停在行尾
        self.cursor = self.line_start_offset(prev_line) + new_col;
    }

    /// 下移一行,尽量保持列。
    pub fn move_down(&mut self) {
        let (line, col) = self.cursor_line_col();
        if line + 1 >= self.line_count() {
            return;
        }
        let next_line = line + 1;
        let next_len = self.line_len(next_line);
        let new_col = if col < next_len { col } else { next_len };
        self.cursor = self.line_start_offset(next_line) + new_col;
    }
}
