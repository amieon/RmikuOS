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
use utils::{get_key, wait_any_key, print_at, Key, bytes_to_str};
use manager::LibraryManager;

const STATUS_ROW: u16 = 1;
const MENU_START: u16 = 3;
const OUTPUT_ROW: u16 = 16;

fn execute_command(cmd: &str, manager: &mut LibraryManager, user_idx: usize, is_admin: bool) {
    match cmd {
        "add" => {
            if is_admin { manager.add_book(user_idx); }
            else { puts("权限不足！\n"); }
        }
        "search" => manager.search_book(),
        "borrow" => manager.borrow_book(user_idx),
        "return" => manager.return_book(user_idx),
        "list" => manager.list_books(),
        "my" => manager.my_borrows(user_idx),
        "users" => {
            if is_admin { manager.list_users(user_idx); }
            else { puts("权限不足！\n"); }
        }
        "backup" => {
            if is_admin {
                puts("启动备份子进程...\n");
                backup::backup_data();
                puts("备份已在后台进行。\n");
            } else {
                puts("权限不足！\n");
            }
        }
        "help" => {
            puts("可用命令：\n");
            puts("  add      - 添加图书（管理员）\n");
            puts("  search   - 查询图书\n");
            puts("  borrow   - 借书\n");
            puts("  return   - 还书\n");
            puts("  list     - 显示所有图书\n");
            puts("  my       - 查看我的借阅\n");
            puts("  users    - 查看所有用户（管理员）\n");
            puts("  backup   - 备份数据（管理员）\n");
            puts("  help     - 显示此帮助\n");
            puts("  exit     - 退出系统\n");
        }
        "exit" => {
            manager.save_all();
            puts("再见！\n");
            exit(0);
        }
        _ => {}
    }
}

fn menu_loop(manager: &mut LibraryManager, user_idx: usize, is_admin: bool) -> ! {
    let commands: &[&str] = if is_admin {
        &["add", "search", "borrow", "return", "list", "my", "users", "backup", "help", "exit"]
    } else {
        &["search", "borrow", "return", "list", "my", "help", "exit"]
    };
    let mut selected = 0usize;

    // 清屏并设置滚动区域（但这里我们手动管理位置，不滚动）
    puts("\x1B[2J\x1B[H");
    puts("\x1B[?25h");  // 显示光标

    let username = bytes_to_str(&manager.users[user_idx].username);
    print_at(STATUS_ROW, 1, "========== 图书馆管理系统 | 用户: ");
    puts(username);
    puts(" ==========");

    print_at(MENU_START, 1, "可用命令 (↑↓ 或 w/s 移动, 回车执行):");

    loop {
        // 绘制菜单项
        for (i, cmd) in commands.iter().enumerate() {
            let row = MENU_START + 1 + i as u16;
            // 清除行
            print_at(row, 1, "\x1B[K");
            // 打印前缀和命令
            let prefix = if i == selected { ">>> " } else { "    " };
            print_at(row, 1, prefix);
            puts(cmd);
        }

        let key = get_key();
        match key {
            Key::Up | Key::W => {
                if selected > 0 { selected -= 1; }
            }
            Key::Down | Key::S => {
                if selected < commands.len() - 1 { selected += 1; }
            }
            Key::Enter => {
                let cmd = commands[selected];
                // 清空输出区域（从 OUTPUT_ROW 到屏幕底部）
                print_at(OUTPUT_ROW, 1, "\x1B[J");
                execute_command(cmd, manager, user_idx, is_admin);
                print_at(OUTPUT_ROW, 1, "\n按任意键返回菜单...");
                wait_any_key();
                // 清除提示
                print_at(OUTPUT_ROW, 1, "\x1B[J");
            }
            _ => {}
        }
    }
}

#[no_mangle]
#[link_section = ".text.entry"]
pub extern "C" fn _start() -> ! {
    let (books, book_count) = storage::load_books();
    let (users, user_count) = storage::load_users();
    let mut manager = LibraryManager::new((books, book_count), (users, user_count));

    let user_idx_opt = auth::login_or_register(&mut manager.users, &mut manager.user_count);
    if user_idx_opt.is_none() {
        puts("退出系统\n");
        exit(0);
    }
    let user_idx = user_idx_opt.unwrap();
    let is_admin = manager.users[user_idx].is_admin;

    menu_loop(&mut manager, user_idx, is_admin);
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    exit(1);
}