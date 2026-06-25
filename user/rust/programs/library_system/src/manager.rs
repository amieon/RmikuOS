use crate::models::{Book, User, MAX_BOOKS, MAX_USERS, MAX_BORROW};
use crate::storage::{save_books, save_users};
use crate::utils::{readline, atoi, bytes_to_str, put_int};
use ulib::io::{puts, put_char};   // 添加 put_char

pub struct LibraryManager {
    pub books: [Book; MAX_BOOKS],
    pub book_count: usize,
    pub users: [User; MAX_USERS],
    pub user_count: usize,
}


impl LibraryManager {
    pub fn new(books: ([Book; MAX_BOOKS], usize), users: ([User; MAX_USERS], usize)) -> Self {
        LibraryManager {
            books: books.0,
            book_count: books.1,
            users: users.0,
            user_count: users.1,
        }
    }

    // ---------- 图书管理 ----------
    pub fn add_book(&mut self, current_user: usize) {
        if !self.users[current_user].is_admin {
            puts("权限不足！需要管理员权限。\n");
            return;
        }
        if self.book_count >= MAX_BOOKS { puts("图书已满！\n"); return; }
        puts("书名: "); let mut title = [0u8; 64]; readline(&mut title);
        puts("作者: "); let mut author = [0u8; 32]; readline(&mut author);
        puts("ISBN: "); let mut isbn = [0u8; 20]; readline(&mut isbn);
        puts("数量: "); let mut num = [0u8; 8]; readline(&mut num);
        let total = atoi(&num);
        if total == 0 { return; }
        let id = self.book_count as u32 + 1;
        let book = Book::new(id, bytes_to_str(&title), bytes_to_str(&author), bytes_to_str(&isbn), total);
        self.books[self.book_count] = book;
        self.book_count += 1;
        puts("添加成功！ID: "); put_int(id as u64); puts("\n");
        self.save_all();
    }

    pub fn list_books(&self) {
        for i in 0..self.book_count {
            let b = &self.books[i];
            puts("ID: "); put_int(b.id as u64);
            puts(" 书名: "); puts(bytes_to_str(&b.title));
            puts(" 可借: "); put_int(b.available as u64);
            puts("/"); put_int(b.total as u64);
            puts("\n");
        }
    }

    pub fn search_book(&self) {
        puts("书名关键字: ");
        let mut kw = [0u8; 64]; readline(&mut kw);
        let kw_str = bytes_to_str(&kw);
        let mut found = false;
        for i in 0..self.book_count {
            if bytes_to_str(&self.books[i].title).contains(kw_str) {
                let b = &self.books[i];
                puts("ID: "); put_int(b.id as u64);
                puts(" 书名: "); puts(bytes_to_str(&b.title));
                puts(" 可借: "); put_int(b.available as u64);
                puts("/"); put_int(b.total as u64);
                puts("\n");
                found = true;
            }
        }
        if !found { puts("未找到\n"); }
    }

    pub fn borrow_book(&mut self, user_idx: usize) {
        let user = &mut self.users[user_idx];
        if user.borrow_count >= MAX_BORROW { puts("您已借满！\n"); return; }
        puts("图书ID: ");
        let mut buf = [0u8; 8]; readline(&mut buf);
        let id = atoi(&buf);
        for i in 0..self.book_count {
            if self.books[i].id == id && self.books[i].available > 0 {
                self.books[i].available -= 1;
                user.borrowed[user.borrow_count] = id;
                user.borrow_count += 1;
                puts("借阅成功！\n");
                self.save_all();
                return;
            }
        }
        puts("该书不存在或已借完\n");
    }

    pub fn return_book(&mut self, user_idx: usize) {
        let user = &mut self.users[user_idx];
        if user.borrow_count == 0 { puts("您未借任何书\n"); return; }
        puts("图书ID: ");
        let mut buf = [0u8; 8]; readline(&mut buf);
        let id = atoi(&buf);
        for i in 0..user.borrow_count {
            if user.borrowed[i] == id {
                // 在图书列表中增加可借数
                for j in 0..self.book_count {
                    if self.books[j].id == id {
                        self.books[j].available += 1;
                        break;
                    }
                }
                // 删除该借阅记录
                for k in i..user.borrow_count-1 {
                    user.borrowed[k] = user.borrowed[k+1];
                }
                user.borrow_count -= 1;
                puts("还书成功！\n");
                self.save_all();
                return;
            }
        }
        puts("您未借该书\n");
    }

    pub fn my_borrows(&self, user_idx: usize) {
        let user = &self.users[user_idx];
        if user.borrow_count == 0 { puts("暂无借阅\n"); return; }
        puts("您借阅的图书ID: ");
        for i in 0..user.borrow_count {
            put_int(user.borrowed[i] as u64);
            put_char(b' ');
        }
        puts("\n");
    }

    pub fn list_users(&self, current_user: usize) {
        if !self.users[current_user].is_admin {
            puts("权限不足！\n");
            return;
        }
        for i in 0..self.user_count {
            let u = &self.users[i];
            puts("ID: "); put_int(u.id as u64);
            puts(" 用户名: "); puts(bytes_to_str(&u.username));
            puts(" 管理员: "); puts(if u.is_admin { "是" } else { "否" });
            puts(" 借阅: "); put_int(u.borrow_count as u64);
            puts("\n");
        }
    }

    pub fn save_all(&self) {
        save_books(&self.books, self.book_count);
        save_users(&self.users, self.user_count);
    }
}