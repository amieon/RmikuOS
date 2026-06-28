#![no_std]
#![no_main]

mod utils;

extern crate alloc;
use alloc::vec::Vec;

use ulib::io::puts;
use ulib::process::exit;
use utils::{put_int};

#[no_mangle]
#[link_section = ".text.entry"]
pub extern "C" fn _start() -> ! {
    let mut v: Vec<i32> = Vec::new();
    for i in 0..100 {
        v.push(i);       
    }

    let sum: i32 = v.iter().sum(); 
    puts("sum = ");
    put_int(sum);
    puts("\n");

    puts("len = ");
    put_int(v.len() as i32);
    puts("\n");

    exit(0);
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    exit(1);
}