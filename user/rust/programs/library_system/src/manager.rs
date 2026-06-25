use crate::models::{Book, MAX_BOOKS};
use crate::storage::{load_books, save_books};
use crate::backup::backup_data;
use crate::utils::{readline, atoi, bytes_to_str, put_int};
use ulib::io::puts;

pub struct LibraryManager {
    books: [Book; MAX_BOOKS],
    count: usize,
    next_id: u32,
}

impl LibraryManager {
    pub fn new() -> Self {
        let (books, count) = load_books();
        let mut max_id = 0;
        for i in 0..count {
            if books[i].id > max_id { max_id = books[i].id; }
        }
        LibraryManager { books, count, next_id: max_id + 1 }
    }

    pub fn add_book(&mut self) {
        if self.count >= MAX_BOOKS {
            puts("图书已满，无法添加！\n");
            return;
        }
        puts("书名: ");
        let mut title = [0u8; 64];
        readline(&mut title);
        puts("作者: ");
        let mut author = [0u8; 32];
        readline(&mut author);
        puts("ISBN: ");
        let mut isbn = [0u8; 20];
        readline(&mut isbn);
        puts("数量: ");
        let mut num = [0u8; 8];
        readline(&mut num);
        let total = atoi(&num);
        if total == 0 { return; }
        let id = self.next_id;
        self.next_id += 1;
        let book = Book::new(id,
                             bytes_to_str(&title),
                             bytes_to_str(&author),
                             bytes_to_str(&isbn),
                             total);
        self.books[self.count] = book;
        self.count += 1;
        puts("图书添加成功！ID: ");
        put_int(id as u64);
        puts("\n");
        self.save();
    }

    pub fn list_all(&self) {
        for i in 0..self.count {
            let book = &self.books[i];
            puts("ID: "); put_int(book.id as u64);
            puts(" 书名: "); puts(bytes_to_str(&book.title));
            puts(" 作者: "); puts(bytes_to_str(&book.author));
            puts(" 总数: "); put_int(book.total as u64);
            puts(" 可借: "); put_int(book.available as u64);
            puts("\n");
        }
    }

    pub fn search_book(&self) {
        puts("请输入书名关键字: ");
        let mut keyword = [0u8; 64];
        readline(&mut keyword);
        let kw = bytes_to_str(&keyword);
        let mut found = false;
        for i in 0..self.count {
            let book = &self.books[i];
            if bytes_to_str(&book.title).contains(kw) {
                puts("找到: "); puts(bytes_to_str(&book.title));
                puts(" (ID: "); put_int(book.id as u64); puts(")\n");
                found = true;
            }
        }
        if !found { puts("未找到匹配的图书。\n"); }
    }

    pub fn borrow_book(&mut self) {
        puts("请输入图书 ID: ");
        let mut buf = [0u8; 8];
        readline(&mut buf);
        let id = atoi(&buf);
        for i in 0..self.count {
            if self.books[i].id == id {
                if self.books[i].available > 0 {
                    self.books[i].available -= 1;
                    puts("借阅成功！\n");
                    self.save();
                } else {
                    puts("该书已全部借出。\n");
                }
                return;
            }
        }
        puts("未找到该书。\n");
    }

    pub fn return_book(&mut self) {
        puts("请输入图书 ID: ");
        let mut buf = [0u8; 8];
        readline(&mut buf);
        let id = atoi(&buf);
        for i in 0..self.count {
            if self.books[i].id == id {
                if self.books[i].available < self.books[i].total {
                    self.books[i].available += 1;
                    puts("还书成功！\n");
                    self.save();
                } else {
                    puts("该书已全部在库，无需归还。\n");
                }
                return;
            }
        }
        puts("未找到该书。\n");
    }

    /// 调用备份（多进程）
    pub fn do_backup(&self) {
        puts("启动备份子进程...\n");
        backup_data();
        puts("备份已在后台进行。\n");
    }

    pub fn save(&self) {
        save_books(&self.books, self.count);
    }

    pub fn get_books(&self) -> &[Book] {
        &self.books[..self.count]
    }
}