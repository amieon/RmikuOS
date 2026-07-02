//! 渲染:把当前文本 + 光标 + 状态栏画成一帧,一次性输出。
//!
//! 治闪屏/白方块乱跳的三个关键:
//!   1. 不用 \x1b[2J 清屏,而是 home(回左上) + 每行 clear_to_eol(清行尾)
//!      覆盖式重绘——没有"全屏空白"的中间态,所以不闪。
//!   2. 整帧拼进一个 Frame(Vec<u8>),最后 flush 一次 write——
//!      终端一次收到完整帧,而不是几十次小写入。
//!   3. 重画时先 hide_cursor,画完把光标定位到编辑位置再 show_cursor——
//!      重画过程中硬件光标不可见,不会看到它乱跳。

use crate::buffer::Buffer;
use crate::terminal::Frame;

pub const SCREEN_ROWS: usize = 24;
pub const SCREEN_COLS: usize = 80;
const CONTENT_ROWS: usize = SCREEN_ROWS - 2; // 留两行给状态栏 + 提示

pub struct Render {
    frame: Frame,
    /// 可视区域第一行对应缓冲区的"行号"(用于上下滚动)
    top_line: usize,
}

impl Render {
    pub fn new() -> Self {
        Render {
            frame: Frame::new(),
            top_line: 0,
        }
    }

    /// 进入编辑器:清一次屏(整个会话只清这一次)
    pub fn enter(&mut self) {
        self.frame.clear();
        self.frame.clear_screen();
        self.frame.flush();
    }

    /// 退出编辑器:清屏 + 显示光标
    pub fn leave(&mut self) {
        self.frame.clear();
        self.frame.clear_screen();
        self.frame.show_cursor();
        self.frame.flush();
    }

    /// 根据光标所在行,调整 top_line,保证光标行在可视区内(滚动)
    fn adjust_scroll(&mut self, buf: &Buffer) {
        let (line, _col) = buf.cursor_line_col();
        if line < self.top_line {
            // 光标跑到可视区上方,把视窗上移
            self.top_line = line;
        } else if line >= self.top_line + CONTENT_ROWS {
            // 光标跑到可视区下方,把视窗下移
            self.top_line = line - CONTENT_ROWS + 1;
        }
    }

    /// 画一帧
    pub fn draw(&mut self, buf: &Buffer, filename: &[u8], message: &[u8]) {
        self.adjust_scroll(buf);

        let frame = &mut self.frame;
        frame.clear();

        // 1. 隐藏光标 + 回左上
        frame.hide_cursor();
        frame.home();

        // 2. 逐行画内容区(覆盖式,每行清行尾)
        for screen_row in 0..CONTENT_ROWS {
            let buf_line = self.top_line + screen_row;

            // 光标定位到这一行行首(屏幕行从 1 开始)
            frame.move_to((screen_row + 1) as u32, 1);

            if buf_line < buf.line_count() {
                // 画这一行的内容(截断到屏幕宽度)
                let start = buf.line_start_offset(buf_line);
                let line_len = buf.line_len(buf_line);
                let show_len = if line_len < SCREEN_COLS {
                    line_len
                } else {
                    SCREEN_COLS
                };
                // 直接把字节拷进帧(假定可显示;非 ASCII 也原样发,终端自行处理)
                for i in 0..show_len {
                    let b = buf.data[start + i];
                    // 控制字符(除已按行处理的)用空格代替,避免乱跳
                    if b >= 32 && b < 127 {
                        frame.put_byte(b);
                    } else {
                        frame.put_byte(b' ');
                    }
                }
            }
            // 清掉这一行光标之后的残留
            frame.clear_to_eol();
        }

        // 3. 状态栏(倒数第二行)
        frame.move_to((SCREEN_ROWS - 1) as u32, 1);
        frame.clear_to_eol();
        frame.put_byte(b' ');
        frame.put_bytes(filename);
        frame.put_bytes(b"  |  Line ");
        let (line, col) = buf.cursor_line_col();
        frame.put_num((line + 1) as u32);
        frame.put_bytes(b" Col ");
        frame.put_num((col + 1) as u32);

        // 4. 提示行(最后一行):若有消息显示消息,否则显示快捷键
        frame.move_to(SCREEN_ROWS as u32, 1);
        frame.clear_to_eol();
        if !message.is_empty() {
            frame.put_byte(b' ');
            frame.put_bytes(message);
        } else {
            frame.put_bytes(b" Ctrl+S save   Ctrl+Q quit");
        }

        // 5. 把编辑光标定位到它在屏幕上的实际位置,然后显示光标
        let cursor_screen_row = line - self.top_line + 1; // 1 起
        let cursor_screen_col = col + 1; // 1 起
        frame.move_to(cursor_screen_row as u32, cursor_screen_col as u32);
        frame.show_cursor();

        // 6. 整帧一次写出
        frame.flush();
    }
}
