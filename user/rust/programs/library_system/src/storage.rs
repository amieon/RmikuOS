use crate::models::{Book, MAX_BOOKS};
use crate::utils::{bytes_to_str, atoi};
use ulib::io::{open, open_create, close, write, read, puts};
use ulib::fs::unlink;

const DATA_FILE: &[u8] = b"/tmp/lib_data.txt";

/// 保存所有图书（先删除旧文件，再 open_create 创建新文件）
pub fn save_books(books: &[Book; MAX_BOOKS], count: usize) {
    let _ = unlink(DATA_FILE);   // 删除旧文件
    let fd = open_create(DATA_FILE);
    if fd < 0 {
        puts("保存失败：无法创建数据文件\n");
        return;
    }
    let mut buf = [0u8; 4096];   // 一次性写入所有内容
    let mut pos = 0;
    for i in 0..count {
        let len = write_book_line(&mut buf[pos..], &books[i]);
        pos += len;
        if pos + 256 > buf.len() {   // 缓冲区快满时先写入
            if write(fd as usize, &buf[..pos]) < 0 {
                puts("写入失败\n");
                close(fd as usize);
                return;
            }
            pos = 0;
        }
    }
    if pos > 0 {
        write(fd as usize, &buf[..pos]);
    }
    close(fd as usize);
    puts("数据已保存。\n");
}

/// 将一本书写入缓冲区，返回写入长度
fn write_book_line(buf: &mut [u8], book: &Book) -> usize {
    let title = bytes_to_str(&book.title);
    let author = bytes_to_str(&book.author);
    let isbn = bytes_to_str(&book.isbn);
    let mut pos = 0;
    pos += write_num(buf, pos, book.id);
    buf[pos] = b','; pos += 1;
    pos += write_str(buf, pos, title);
    buf[pos] = b','; pos += 1;
    pos += write_str(buf, pos, author);
    buf[pos] = b','; pos += 1;
    pos += write_str(buf, pos, isbn);
    buf[pos] = b','; pos += 1;
    pos += write_num(buf, pos, book.total);
    buf[pos] = b','; pos += 1;
    pos += write_num(buf, pos, book.available);
    buf[pos] = b'\n'; pos += 1;
    pos
}

fn write_str(buf: &mut [u8], start: usize, s: &str) -> usize {
    let bytes = s.as_bytes();
    let len = if bytes.len() < buf.len() - start { bytes.len() } else { buf.len() - start - 1 };
    buf[start..start+len].copy_from_slice(&bytes[..len]);
    len
}

fn write_num(buf: &mut [u8], start: usize, mut x: u32) -> usize {
    if x == 0 {
        buf[start] = b'0';
        return 1;
    }
    let mut digits = [0u8; 10];
    let mut n = 0;
    while x > 0 {
        digits[n] = b'0' + (x % 10) as u8;
        x /= 10;
        n += 1;
    }
    for i in 0..n {
        buf[start + i] = digits[n - 1 - i];
    }
    n
}

/// 从文件加载图书（一次性读取到缓冲区）
pub fn load_books() -> ([Book; MAX_BOOKS], usize) {
    let mut books = [Book::new(0, "", "", "", 0); MAX_BOOKS];
    let mut count = 0;
    let fd = open(DATA_FILE);
    if fd < 0 {
        puts("没有现有数据文件，将创建新库。\n");
        return (books, 0);
    }
    let mut buf = [0u8; 4096];
    let n = read(fd as usize, &mut buf);
    close(fd as usize);
    if n <= 0 { return (books, 0); }

    let mut start = 0;
    for i in 0..(n as usize) {
        if buf[i] == b'\n' {
            let line = &buf[start..i];
            if !line.is_empty() && count < MAX_BOOKS {
                if let Some(book) = parse_book_line(line) {
                    books[count] = book;
                    count += 1;
                }
            }
            start = i + 1;
        }
    }
    (books, count)
}

fn parse_book_line(line: &[u8]) -> Option<Book> {
    let mut fields = [0usize; 6];
    let mut cnt = 0;
    let mut pos = 0;
    while pos < line.len() && cnt < 6 {
        if line[pos] == b',' {
            fields[cnt] = pos;
            cnt += 1;
        }
        pos += 1;
    }
    if cnt < 5 { return None; }
    let id_str = &line[..fields[0]];
    let title_str = &line[fields[0]+1..fields[1]];
    let author_str = &line[fields[1]+1..fields[2]];
    let isbn_str = &line[fields[2]+1..fields[3]];
    let total_str = &line[fields[3]+1..fields[4]];
    let avail_str = &line[fields[4]+1..line.len()];

    let id = atoi(id_str);
    let total = atoi(total_str);
    let available = atoi(avail_str);
    let title = core::str::from_utf8(title_str).unwrap_or("");
    let author = core::str::from_utf8(author_str).unwrap_or("");
    let isbn = core::str::from_utf8(isbn_str).unwrap_or("");
    Some(Book::new(id, title, author, isbn, total))
}