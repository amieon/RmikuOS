use crate::utils::copy_str_to_buf;

pub const TITLE_LEN: usize = 64;
pub const AUTHOR_LEN: usize = 32;
pub const ISBN_LEN: usize = 20;
pub const NAME_LEN: usize = 16;
pub const PASS_LEN: usize = 16;
pub const MAX_BOOKS: usize = 100;
pub const MAX_USERS: usize = 50;
pub const MAX_BORROW: usize = 5;

#[derive(Clone, Copy)]
pub struct Book {
    pub id: u32,
    pub title: [u8; TITLE_LEN],
    pub author: [u8; AUTHOR_LEN],
    pub isbn: [u8; ISBN_LEN],
    pub total: u32,
    pub available: u32,
}

impl Book {
    pub fn new(id: u32, title: &str, author: &str, isbn: &str, total: u32) -> Self {
        let mut b = Book {
            id,
            title: [0; TITLE_LEN],
            author: [0; AUTHOR_LEN],
            isbn: [0; ISBN_LEN],
            total,
            available: total,
        };
        copy_str_to_buf(&mut b.title, title);
        copy_str_to_buf(&mut b.author, author);
        copy_str_to_buf(&mut b.isbn, isbn);
        b
    }
}

#[derive(Clone, Copy)]
pub struct User {
    pub id: u32,
    pub username: [u8; NAME_LEN],
    pub password: [u8; PASS_LEN],
    pub is_admin: bool,
    pub borrowed: [u32; MAX_BORROW],
    pub borrow_count: usize,
}

impl User {
    pub fn new(id: u32, username: &str, password: &str, is_admin: bool) -> Self {
        let mut u = User {
            id,
            username: [0; NAME_LEN],
            password: [0; PASS_LEN],
            is_admin,
            borrowed: [0; MAX_BORROW],
            borrow_count: 0,
        };
        copy_str_to_buf(&mut u.username, username);
        copy_str_to_buf(&mut u.password, password);
        u
    }
}