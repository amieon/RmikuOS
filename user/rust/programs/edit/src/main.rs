#![no_std]
#![no_main]

extern crate alloc;   // 使用动态内存

use alloc::vec::Vec;
use alloc::format;
use core::str;

use ulib::io::{open, close, read, write, puts, put_char};
use ulib::process::exit;

// ---------- 工具函数（与之前相同） ----------
fn print_at(row: u16, col: u16, s: &str) {
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

fn readline(buf: &mut [u8]) -> usize {
    let mut i = 0;
    while i < buf.len() - 1 {
        let mut ch = 0u8;
        let n = ulib::io::read(0, core::slice::from_mut(&mut ch));
        if n <= 0 { continue; }
        if ch == b'\r' { ch = b'\n'; }
        if ch == b'\n' { put_char(b'\n'); break; }
        if ch == 8 || ch == 127 {
            if i > 0 {
                i -= 1;
                put_char(8); put_char(b' '); put_char(8);
            }
            continue;
        }
        buf[i] = ch; i += 1; put_char(ch);
    }
    buf[i] = 0;
    i
}

// ---------- 编辑器核心 ----------
const SCREEN_ROWS: u16 = 24;
const SCREEN_COLS: u16 = 80;

static mut BUFFER: Vec<u8> = Vec::new();         // 动态缓冲区
static mut CURSOR_OFFSET: usize = 0;             // 当前光标在缓冲区中的字节偏移
static mut DISPLAY_START: usize = 0;             // 当前可视区域起始偏移
static mut FILENAME: [u8; 64] = [0; 64];
static mut FILENAME_LEN: usize = 0;

// 加载文件到 Vec（自动扩容）
fn load_file(path: &str) -> bool {
    let fd = open(path.as_bytes(), 0); // O_RDONLY
    if fd < 0 { return false; }
    let mut buf = [0u8; 4096];
    let mut total = 0;
    unsafe {
        BUFFER.clear();
    }
    loop {
        let n = read(fd as usize, &mut buf);
        if n <= 0 { break; }
        unsafe {
            BUFFER.extend_from_slice(&buf[..n as usize]);
        }
        total += n as usize;
    }
    close(fd as usize);
    true
}

// 保存缓冲区到文件（覆盖写入）
fn save_file(path: &str) -> bool {
    let fd = open(path.as_bytes(), 0x41); // O_CREAT | O_WRONLY | O_TRUNC
    if fd < 0 { return false; }
    unsafe {
        let written = write(fd as usize, &BUFFER[..]);
        close(fd as usize);
        written >= 0
    }
}

// 计算光标所在行列（从0开始）
fn get_cursor_line_col() -> (u16, u16) {
    unsafe {
        let mut line = 0;
        let mut col = 0;
        for i in 0..CURSOR_OFFSET {
            if BUFFER[i] == b'\n' {
                line += 1;
                col = 0;
            } else {
                col += 1;
            }
        }
        (line, col)
    }
}

// 刷新屏幕（全量重绘）
fn refresh_screen() {
    unsafe {
        let content_rows = SCREEN_ROWS - 2;
        let content_cols = SCREEN_COLS as usize;

        // 清屏
        puts("\x1B[2J\x1B[H");

        // 从 DISPLAY_START 开始显示
        let mut pos = DISPLAY_START;
        let mut row = 0;
        while row < content_rows && pos < BUFFER.len() {
            let mut line_end = pos;
            let mut col = 0;
            // 找到本行结束位置（换行或列宽）
            while line_end < BUFFER.len() && BUFFER[line_end] != b'\n' && col < content_cols {
                line_end += 1;
                col += 1;
            }
            // 输出行内容（如果是空行，显示一个空白行？实际上我们后面会清空行）
            if col > 0 {
                let line_slice = &BUFFER[pos..line_end];
                if let Ok(s) = str::from_utf8(line_slice) {
                    print_at(row + 1, 1, s);
                }
            }
            // 清空行尾（防止残留）
            print_at(row + 1, 1 + col as u16, "\x1B[K");
            // 处理换行
            if line_end < BUFFER.len() && BUFFER[line_end] == b'\n' {
                pos = line_end + 1; // 跳过 '\n'
            } else {
                pos = line_end;
                // 如果是因为列宽截断，但本行没有换行，则换行（我们强制换行显示）
                // 这里不处理，简单地将 pos 设为 line_end，可能继续显示同一行内容，但会被截断。
                // 为了简单，我们设定如果一行超过列宽，就截断，不换行。
                // 更好的是实现自动换行，但暂时忽略。
                break;
            }
            row += 1;
        }

        // 清空剩余行
        while row < content_rows {
            print_at(row + 1, 1, "\x1B[K");
            row += 1;
        }

        // 状态栏
        let (line, col) = get_cursor_line_col();
        let name = core::str::from_utf8(&FILENAME[..FILENAME_LEN]).unwrap_or("");
        let status = format!(" {} | Line: {} Col: {} ", name, line+1, col+1);
        let status_bytes = status.as_bytes();
        let status_len = if status_bytes.len() < SCREEN_COLS as usize { status_bytes.len() } else { (SCREEN_COLS - 1) as usize };
        print_at(SCREEN_ROWS - 1, 1, core::str::from_utf8(&status_bytes[..status_len]).unwrap_or(""));

        // 提示行
        print_at(SCREEN_ROWS, 1, " Ctrl+S 保存  Ctrl+Q 退出");

        // 移动光标到实际位置
        // 计算光标在显示区域的位置
        let mut display_row = 0;
        let mut pos2 = DISPLAY_START;
        let mut found = false;
        while pos2 <= CURSOR_OFFSET && display_row < content_rows {
            let mut end = pos2;
            let mut cols = 0;
            while end < BUFFER.len() && BUFFER[end] != b'\n' && cols < content_cols {
                end += 1;
                cols += 1;
            }
            if pos2 <= CURSOR_OFFSET && CURSOR_OFFSET <= end {
                let col_offset = CURSOR_OFFSET - pos2;
                print_at(display_row + 1, 1 + col_offset as u16, "");
                found = true;
                break;
            }
            if end < BUFFER.len() && BUFFER[end] == b'\n' {
                pos2 = end + 1;
            } else {
                pos2 = end;
            }
            display_row += 1;
        }
        if !found {
            // 如果光标不在可视区，将其置于左上角（或最后一行的末尾）
            print_at(1, 1, "");
        }
    }
}

// 移动光标（带滚动）
fn move_cursor(new_offset: usize) {
    unsafe {
        if new_offset > BUFFER.len() { return; }
        CURSOR_OFFSET = new_offset;
        // 简单滚动：确保光标所在行在可视区域内
        // 计算光标所在行号
        let mut line = 0;
        let mut pos = 0;
        while pos < CURSOR_OFFSET {
            if unsafe { BUFFER[pos] } == b'\n' { line += 1; }
            pos += 1;
        }
        let content_rows = SCREEN_ROWS - 2;
        // 计算 DISPLAY_START 对应的行号（我们直接调整，使光标位于约一半行处）
        let target_line = line as u16;
        let display_row_center = (content_rows / 2) as u16;
        let start_line = if target_line > display_row_center { target_line - display_row_center } else { 0 };
        // 找到该行的起始偏移
        let mut new_display = 0;
        let mut current_line = 0;
        let mut pos2 = 0;
        while pos2 < BUFFER.len() && current_line < start_line {
            if unsafe { BUFFER[pos2] } == b'\n' { current_line += 1; }
            pos2 += 1;
        }
        DISPLAY_START = pos2;
    }
}

// 插入字符（自动扩容）
fn insert_char(ch: u8) {
    unsafe {
        if CURSOR_OFFSET > BUFFER.len() { return; }
        BUFFER.insert(CURSOR_OFFSET, ch);
        CURSOR_OFFSET += 1;
    }
}

// 删除光标前一个字符
fn delete_char() {
    unsafe {
        if CURSOR_OFFSET == 0 || CURSOR_OFFSET > BUFFER.len() { return; }
        BUFFER.remove(CURSOR_OFFSET - 1);
        CURSOR_OFFSET -= 1;
    }
}

// ---------- 按键处理 ----------
enum Key {
    Up, Down, Left, Right,
    Char(u8),
    Enter,
    Backspace,
    CtrlS,
    CtrlQ,
    Other,
}

fn get_key_advanced() -> Key {
    let mut ch = 0u8;
    if ulib::io::read(0, core::slice::from_mut(&mut ch)) <= 0 {
        return Key::Other;
    }
    if ch == 0x1B {
        let mut seq = [0u8; 2];
        if ulib::io::read(0, &mut seq[..1]) <= 0 { return Key::Other; }
        if seq[0] == b'[' {
            if ulib::io::read(0, &mut seq[1..2]) <= 0 { return Key::Other; }
            match seq[1] {
                b'A' => return Key::Up,
                b'B' => return Key::Down,
                b'C' => return Key::Right,
                b'D' => return Key::Left,
                _ => {}
            }
        }
        return Key::Other;
    }
    if ch == b'\n' || ch == b'\r' { return Key::Enter; }
    if ch == 8 || ch == 127 { return Key::Backspace; }
    if ch == 0x13 { return Key::CtrlS; } // Ctrl+S
    if ch == 0x11 { return Key::CtrlQ; } // Ctrl+Q
    if ch >= 32 && ch < 127 { return Key::Char(ch); }
    Key::Other
}

// ---------- 主程序 ----------
#[no_mangle]
#[link_section = ".text.entry"]
pub extern "C" fn _start() -> ! {
    // 初始化 Vec（全局分配器已就绪）
    unsafe {
        BUFFER = Vec::new();
    }

    // 获取文件名
    puts("请输入文件名: ");
    let mut fname = [0u8; 64];
    let len = readline(&mut fname);
    if len == 0 { exit(0); }
    unsafe {
        FILENAME[..len].copy_from_slice(&fname[..len]);
        FILENAME_LEN = len;
    }
    let name_str = unsafe { core::str::from_utf8(&FILENAME[..FILENAME_LEN]).unwrap_or("") };

    if !load_file(name_str) {
        puts("新文件\n");
    }

    // 初始化光标位置
    unsafe {
        CURSOR_OFFSET = 0;
        DISPLAY_START = 0;
    }

    loop {
        refresh_screen();

        let key = get_key_advanced();
        match key {
            Key::Up => {
                // 移动到上一行
                let (line, col) = get_cursor_line_col();
                if line > 0 {
                    // 找到上一行开头
                    let mut new_pos = unsafe { CURSOR_OFFSET };
                    while new_pos > 0 && unsafe { BUFFER[new_pos-1] } != b'\n' {
                        new_pos -= 1;
                    }
                    new_pos -= 1; // 跳过 '\n'
                    if new_pos > 0 {
                        let mut target = new_pos;
                        let mut cnt = 0;
                        while target < unsafe { BUFFER.len() } && unsafe { BUFFER[target] } != b'\n' && cnt < col as usize {
                            target += 1;
                            cnt += 1;
                        }
                        move_cursor(target);
                    }
                }
            }
            Key::Down => {
                let (line, col) = get_cursor_line_col();
                // 找到下一行开头
                let mut new_pos = unsafe { CURSOR_OFFSET };
                while new_pos < unsafe { BUFFER.len() } && unsafe { BUFFER[new_pos] } != b'\n' {
                    new_pos += 1;
                }
                if new_pos < unsafe { BUFFER.len() } {
                    new_pos += 1; // 跳过 '\n'
                    let mut target = new_pos;
                    let mut cnt = 0;
                    while target < unsafe { BUFFER.len() } && unsafe { BUFFER[target] } != b'\n' && cnt < col as usize {
                        target += 1;
                        cnt += 1;
                    }
                    move_cursor(target);
                }
            }
            Key::Left => {
                if unsafe { CURSOR_OFFSET } > 0 {
                    move_cursor(unsafe { CURSOR_OFFSET - 1 });
                }
            }
            Key::Right => {
                if unsafe { CURSOR_OFFSET } < unsafe { BUFFER.len() } {
                    move_cursor(unsafe { CURSOR_OFFSET + 1 });
                }
            }
            Key::Char(ch) if ch >= 32 && ch < 127 => {
                insert_char(ch);
            }
            Key::Enter => {
                insert_char(b'\n');
            }
            Key::Backspace => {
                delete_char();
            }
            Key::CtrlS => {
                if save_file(name_str) {
                    print_at(SCREEN_ROWS, 1, "\x1B[K文件已保存");
                } else {
                    print_at(SCREEN_ROWS, 1, "\x1B[K保存失败");
                }
            }
            Key::CtrlQ => {
                puts("\x1B[2J\x1B[H");
                exit(0);
            }
            _ => {}
        }
    }
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    exit(1);
}