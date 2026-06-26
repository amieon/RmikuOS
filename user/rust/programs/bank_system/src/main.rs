#![no_std]
#![no_main]

mod models;
mod storage;
mod manager;
mod interest;
mod utils;

use ulib::io::{puts, put_char};
use ulib::process::exit;
use utils::{get_key, wait_any_key, print_at, Key, bytes_to_str};
use manager::BankManager;

const STATUS_ROW: u16 = 1;
const MENU_START: u16 = 3;
const OUTPUT_ROW: u16 = 16;

const MENU_ITEMS: [&str; 8] = [
    "创建账户",
    "存款",
    "取款",
    "转账",
    "查询余额",
    "列出所有账户",
    "[多进程] 批量结息",
    "退出",
];

fn execute_command(idx: usize, manager: &mut BankManager) -> bool {
    match idx {
        0 => manager.create_account(),
        1 => manager.deposit(),
        2 => manager.withdraw(),
        3 => manager.transfer(),
        4 => manager.query(),
        5 => manager.list_all(),
        6 => {
            puts("输入年利率(%, 例如 5): ");
            let mut buf = [0u8; 8];
            utils::readline(&mut buf);
            let rate = utils::atoi(&buf);
            interest::apply_interest_parallel(manager, rate);
        }
        7 => return false, // exit
        _ => {}
    }
    true
}

fn menu_loop(manager: &mut BankManager) {
    let mut selected = 0usize;

    puts("\x1B[2J\x1B[H");
    puts("\x1B[?25h");

    print_at(STATUS_ROW, 1, "========== 银行系统 | 欢迎使用 ==========");
    print_at(MENU_START, 1, "请选择操作 (↑↓ 或 w/s 移动, 回车执行):");

    loop {
        for (i, item) in MENU_ITEMS.iter().enumerate() {
            let row = MENU_START + 1 + i as u16;
            let prefix = if i == selected { ">>> " } else { "    " };
            print_at(row, 1, prefix);
            puts(item);
            puts("\x1B[K");
        }

        let key = get_key();
        match key {
            Key::Up | Key::W => {
                if selected > 0 { selected -= 1; }
            }
            Key::Down | Key::S => {
                if selected < MENU_ITEMS.len() - 1 { selected += 1; }
            }
            Key::Enter => {
                print_at(OUTPUT_ROW, 1, "\x1B[J");
                let should_continue = execute_command(selected, manager);
                if !should_continue { break; }
                print_at(OUTPUT_ROW, 1, "\n按任意键返回菜单...");
                wait_any_key();
                print_at(OUTPUT_ROW, 1, "\x1B[J");
            }
            _ => {}
        }
    }
}

#[no_mangle]
#[link_section = ".text.entry"]
pub extern "C" fn _start() -> ! {
    let mut manager = BankManager::new();
    menu_loop(&mut manager);
    manager.save();
    puts("再见！\n");
    exit(0);
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    exit(1);
}