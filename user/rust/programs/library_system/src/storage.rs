use crate::models::{Book, User, MAX_BOOKS, MAX_USERS, MAX_BORROW};
use crate::utils::{bytes_to_str, atoi, copy_str_to_buf};
use ulib::io::{open, open_create, close, write, read, puts};
use ulib::fs::unlink;
use ulib::flag::*;

const BOOK_FILE: &[u8] = b"/tmp/books.txt";
const USER_FILE: &[u8] = b"/tmp/users.txt";

// ---------- 图书存储 ----------
pub fn save_books(books: &[Book; MAX_BOOKS], count: usize) {
    let _ = unlink(BOOK_FILE);
    let fd = open_create(BOOK_FILE,O_RDWR);
    if fd < 0 { puts("保存图书失败\n"); return; }
    let mut buf = [0u8; 4096];
    let mut pos = 0;
    for i in 0..count {
        let len = write_book_line(&mut buf[pos..], &books[i]);
        pos += len;
        if pos + 256 > buf.len() {
            write(fd as usize, &buf[..pos]);
            pos = 0;
        }
    }
    if pos > 0 { write(fd as usize, &buf[..pos]); }
    close(fd as usize);
}

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

pub fn load_books() -> ([Book; MAX_BOOKS], usize) {
    let mut books = [Book::new(0, "", "", "", 0); MAX_BOOKS];
    let mut count = 0;
    let fd = open(BOOK_FILE,O_RDWR);
    if fd < 0 { return (books, 0); }
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
        if line[pos] == b',' { fields[cnt] = pos; cnt += 1; }
        pos += 1;
    }
    if cnt < 5 { return None; }
    let id = atoi(&line[..fields[0]]);
    let title = core::str::from_utf8(&line[fields[0]+1..fields[1]]).unwrap_or("");
    let author = core::str::from_utf8(&line[fields[1]+1..fields[2]]).unwrap_or("");
    let isbn = core::str::from_utf8(&line[fields[2]+1..fields[3]]).unwrap_or("");
    let total = atoi(&line[fields[3]+1..fields[4]]);
    let available = atoi(&line[fields[4]+1..line.len()]);
    let mut book = Book::new(id, title, author, isbn, total);
    book.available = available;   // 使用文件中的 available
    Some(book)
}

// ---------- 用户存储 ----------
pub fn save_users(users: &[User; MAX_USERS], count: usize) {
    let _ = unlink(USER_FILE);
    let fd = open_create(USER_FILE,O_RDWR);
    if fd < 0 { puts("保存用户失败\n"); return; }
    let mut buf = [0u8; 4096];
    let mut pos = 0;
    for i in 0..count {
        let len = write_user_line(&mut buf[pos..], &users[i]);
        pos += len;
        if pos + 256 > buf.len() {
            write(fd as usize, &buf[..pos]);
            pos = 0;
        }
    }
    if pos > 0 { write(fd as usize, &buf[..pos]); }
    close(fd as usize);
}

fn write_user_line(buf: &mut [u8], user: &User) -> usize {
    let username = bytes_to_str(&user.username);
    let password = bytes_to_str(&user.password);
    let mut pos = 0;
    pos += write_num(buf, pos, user.id);
    buf[pos] = b','; pos += 1;
    pos += write_str(buf, pos, username);
    buf[pos] = b','; pos += 1;
    pos += write_str(buf, pos, password);
    buf[pos] = b','; pos += 1;
    buf[pos] = if user.is_admin { b'1' } else { b'0' }; pos += 1;
    // 借阅列表：borrow_count, id1, id2, ...
    buf[pos] = b','; pos += 1;
    pos += write_num(buf, pos, user.borrow_count as u32);
    for i in 0..user.borrow_count {
        buf[pos] = b','; pos += 1;
        pos += write_num(buf, pos, user.borrowed[i]);
    }
    buf[pos] = b'\n'; pos += 1;
    pos
}

pub fn load_users() -> ([User; MAX_USERS], usize) {
    let mut users = [User::new(0, "", "", false); MAX_USERS];
    let mut count = 0;
    let fd = open(USER_FILE,O_RDWR);
    if fd < 0 { return (users, 0); }
    let mut buf = [0u8; 4096];
    let n = read(fd as usize, &mut buf);
    close(fd as usize);
    if n <= 0 { return (users, 0); }
    let mut start = 0;
    for i in 0..(n as usize) {
        if buf[i] == b'\n' {
            let line = &buf[start..i];
            if !line.is_empty() && count < MAX_USERS {
                if let Some(user) = parse_user_line(line) {
                    users[count] = user;
                    count += 1;
                }
            }
            start = i + 1;
        }
    }
    (users, count)
}

fn parse_user_line(line: &[u8]) -> Option<User> {
    // 格式: id,username,password,is_admin,borrow_count,id1,id2,...
    let mut fields = [0usize; 20];
    let mut cnt = 0;
    let mut pos = 0;
    while pos < line.len() && cnt < 20 {
        if line[pos] == b',' { fields[cnt] = pos; cnt += 1; }
        pos += 1;
    }
    if cnt < 4 { return None; }
    let id = atoi(&line[..fields[0]]);
    let username = core::str::from_utf8(&line[fields[0]+1..fields[1]]).unwrap_or("");
    let password = core::str::from_utf8(&line[fields[1]+1..fields[2]]).unwrap_or("");
    let is_admin = line[fields[2]+1] == b'1';
    let borrow_count = if cnt > 4 { atoi(&line[fields[3]+1..fields[4]]) } else { 0 } as usize;
    let mut user = User::new(id, username, password, is_admin);
    user.borrow_count = borrow_count;
    for i in 0..borrow_count.min(MAX_BORROW) {
        if i + 4 < cnt {
            let start = fields[3+i+1] + 1;
            let end = if i + 5 < cnt { fields[3+i+2] } else { line.len() };
            user.borrowed[i] = atoi(&line[start..end]);
        }
    }
    Some(user)
}

// 辅助函数
fn write_str(buf: &mut [u8], start: usize, s: &str) -> usize {
    let bytes = s.as_bytes();
    let len = if bytes.len() < buf.len() - start { bytes.len() } else { buf.len() - start - 1 };
    buf[start..start+len].copy_from_slice(&bytes[..len]);
    len
}
fn write_num(buf: &mut [u8], start: usize, mut x: u32) -> usize {
    if x == 0 { buf[start] = b'0'; return 1; }
    let mut digits = [0u8; 10];
    let mut n = 0;
    while x > 0 { digits[n] = b'0' + (x % 10) as u8; x /= 10; n += 1; }
    for i in 0..n { buf[start + i] = digits[n - 1 - i]; }
    n
}