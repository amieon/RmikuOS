use crate::utils::copy_str_to_buf;

pub const TITLE_LEN: usize = 64;
pub const AUTHOR_LEN: usize = 32;
pub const ISBN_LEN: usize = 20;
pub const MAX_BOOKS: usize = 100;

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