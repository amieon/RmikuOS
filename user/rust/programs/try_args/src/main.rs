#![no_std]
#![no_main]

use ulib::args::Args;
use ulib::io::{read,puts,puts_bytes,put_int};
use ulib::process::exit;


#[no_mangle]
#[link_section = ".text.entry"]
pub extern "C" fn _start(argc: usize, argv: *const *const u8) -> ! {
    let args = unsafe { Args::new(argc, argv) };

    puts("argc = ");
    put_int(args.len() as u64);
    puts("\n");

    for i in 0..args.len() {
        if let Some(s) = args.get(i) {
            puts("argv[");
            put_int(i as u64);
            puts("] = ");
            puts_bytes(s);        
            puts("\n");
        }
    }

    exit(0);
    
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    exit(1);
}