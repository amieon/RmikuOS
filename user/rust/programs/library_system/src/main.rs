#![no_std]
#![no_main]

mod models;
mod storage;
mod auth;
mod manager;
mod backup;
mod utils;

use ulib::io::puts;
use ulib::process::exit;
use utils::readline;
use manager::LibraryManager;

fn print_user_menu(is_admin: bool) {
    puts("\n========== 主菜单 ==========\n");
    puts("1. 查询图书\n");
    puts("2. 借书\n");
    puts("3. 还书\n");
    puts("4. 我的借阅\n");
    if is_admin {
        puts("5. 添加图书\n");
        puts("6. 查看所有用户\n");
        puts("7. 备份数据（多进程）\n");
    }
    puts("0. 退出\n");
    puts("请选择: ");
}

#[no_mangle]
#[link_section = ".text.entry"]
pub extern "C" fn _start() -> ! {
    puts("📚 图书馆管理系统 (含用户登录) 启动...\n");

    let (books, book_count) = storage::load_books();
    let (users, user_count) = storage::load_users();
    let mut manager = LibraryManager::new((books, book_count), (users, user_count));

    // 登录/注册
    let user_idx_opt = auth::login_or_register(&mut manager.users, &mut manager.user_count);
    if user_idx_opt.is_none() {
        puts("退出系统\n");
        exit(0);
    }
    let user_idx = user_idx_opt.unwrap();
    let is_admin = manager.users[user_idx].is_admin;
    puts("欢迎回来，");
    puts(crate::utils::bytes_to_str(&manager.users[user_idx].username));
    puts("！\n");

    loop {
        print_user_menu(is_admin);
        let mut buf = [0u8; 4];
        let len = readline(&mut buf);
        if len == 0 { continue; }
        let choice = buf[0] - b'0';
        match choice {
            0 => break,
            1 => manager.search_book(),
            2 => manager.borrow_book(user_idx),
            3 => manager.return_book(user_idx),
            4 => manager.my_borrows(user_idx),
            5 if is_admin => manager.add_book(user_idx),
            6 if is_admin => manager.list_users(user_idx),
            7 if is_admin => {
                puts("启动备份子进程...\n");
                backup::backup_data();
                puts("备份已在后台进行。\n");
            }
            _ => puts("无效选项\n"),
        }
    }

    manager.save_all();
    puts("再见！\n");
    exit(0);
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    exit(1);
}