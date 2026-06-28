#![no_std]
#![no_main]

extern crate alloc;
use alloc::vec::Vec;

mod buffer;
mod input;
mod render;
mod terminal;

use buffer::Buffer;
use input::{read_key, Key};
use render::Render;

use ulib::io::{close, open, read, write, puts};
use ulib::process::exit;

// open flags(和你内核 flag.rs 对齐)
const O_WRONLY: usize = 1;
const O_CREAT: usize = 0x40;
const O_TRUNC: usize = 0x200;
const O_RDONLY: usize = 0;

/// 从文件读全部内容到 Vec。返回 None 表示文件打不开(新文件)。
fn load_file(path: &[u8]) -> Option<Vec<u8>> {
    let fd = open(path, O_RDONLY);
    if fd < 0 {
        return None;
    }
    let fd = fd as usize;
    let mut content = Vec::new();
    let mut buf = [0u8; 512];
    loop {
        let n = read(fd, &mut buf);
        if n <= 0 {
            break;
        }
        content.extend_from_slice(&buf[..n as usize]);
    }
    close(fd);
    Some(content)
}

/// 把缓冲区写回文件(覆盖)。返回是否成功。
fn save_file(path: &[u8], data: &[u8]) -> bool {
    let fd = open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if fd < 0 {
        return false;
    }
    let fd = fd as usize;
    let mut written = 0;
    while written < data.len() {
        let n = write(fd, &data[written..]);
        if n <= 0 {
            close(fd);
            return false;
        }
        written += n as usize;
    }
    close(fd);
    true
}

/// 取 C 字符串长度(\0 结尾)
unsafe fn cstr_len(ptr: *const u8) -> usize {
    let mut len = 0;
    while *ptr.add(len) != 0 {
        len += 1;
    }
    len
}

#[no_mangle]
#[link_section = ".text.entry"]
pub extern "C" fn _start(argc: usize, argv: *const *const u8) -> ! {
    // 需要一个文件名参数:editor <filename>
    if argc < 2 {
        puts("usage: editor <filename>\n");
        exit(1);
    }

    // 取 argv[1] 作为文件名
    let filename: &[u8] = unsafe {
        let ptr = *argv.add(1);
        if ptr.is_null() {
            puts("editor: bad filename\n");
            exit(1);
        }
        let len = cstr_len(ptr);
        core::slice::from_raw_parts(ptr, len)
    };

    // 加载文件(打不开就当新文件,空缓冲)
    let mut buf = Buffer::new();
    match load_file(filename) {
        Some(content) => buf.load(&content),
        None => buf.load(&[]),
    }

    let mut render = Render::new();
    render.enter();

    // 消息行(保存提示等),空表示显示默认快捷键提示
    let mut message: Vec<u8> = Vec::new();

    loop {
        render.draw(&buf, filename, &message);

        // 画完一帧后,这次的消息就过期了(下次按键清掉)
        let key = read_key();
        message.clear();

        match key {
            Key::Up => buf.move_up(),
            Key::Down => buf.move_down(),
            Key::Left => buf.move_left(),
            Key::Right => buf.move_right(),
            Key::Char(ch) => buf.insert(ch),
            Key::Enter => buf.insert(b'\n'),
            Key::Backspace => buf.backspace(),
            Key::CtrlS => {
                if save_file(filename, &buf.data) {
                    message.extend_from_slice(b"saved");
                } else {
                    message.extend_from_slice(b"save failed");
                }
            }
            Key::CtrlQ => {
                render.leave();
                exit(0);
            }
            Key::Other => {}
        }
    }
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    exit(1);
}
