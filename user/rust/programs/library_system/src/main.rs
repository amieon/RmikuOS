#![no_std]
#![no_main]

mod models;
mod storage;
mod manager;
mod backup;
mod utils;

use ulib::io::puts;
use ulib::process::exit;
use crate::utils::readline;

fn print_menu() {
    puts("\n========== 图书馆管理系统 ==========\n");
    puts("1. 添加图书\n");
    puts("2. 查询图书\n");
    puts("3. 借书\n");
    puts("4. 还书\n");
    puts("5. 显示所有图书\n");
    puts("6. 多进程备份数据\n");
    puts("0. 退出\n");
    puts("请选择: ");
}

#[no_mangle]
#[link_section = ".text.entry"]
pub extern "C" fn _start() -> ! {
    puts("📚 图书馆管理系统启动...\n");
    let mut manager = manager::LibraryManager::new();

    loop {
        print_menu();
        let mut buf = [0u8; 4];
        let len = readline(&mut buf);
        if len == 0 { continue; }
        let choice = buf[0] - b'0';
        match choice {
            0 => break,
            1 => manager.add_book(),
            2 => manager.search_book(),
            3 => manager.borrow_book(),
            4 => manager.return_book(),
            5 => manager.list_all(),
            6 => manager.do_backup(),
            _ => puts("无效选项。\n"),
        }
    }

    manager.save();
    puts("再见！\n");
    exit(0);
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    exit(1);
}