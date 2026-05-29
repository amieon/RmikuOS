//! LoongArch64 trap handling.
//!
//! This is a kernel bring-up trap framework, not rCore's batch-app framework.
//! It does not call run_next_app(); unsupported traps are logged and then panic.

mod context;

use core::arch::{asm, global_asm};
use core::fmt::{self, Write};

pub use context::TrapContext;

global_asm!(include_str!("trap.S"));
global_asm!(include_str!("tlb_refill.S"));


const ECODE_INT: usize = 0x00;
const ECODE_PIL: usize = 0x01;
const ECODE_PIS: usize = 0x02;
const ECODE_PIF: usize = 0x03;
const ECODE_PME: usize = 0x04;
const ECODE_PNR: usize = 0x05;
const ECODE_PNX: usize = 0x06;
const ECODE_PPI: usize = 0x07;
const ECODE_ADEF_ADEM: usize = 0x08;
const ECODE_ALE: usize = 0x09;
const ECODE_SYS: usize = 0x0b;
const ECODE_BRK: usize = 0x0c;
const ECODE_INE: usize = 0x0d;
const ECODE_IPE: usize = 0x0e;

const ESTAT_IS_TIMER: usize = 1 << 11;

macro_rules! trap_println {
    ($($arg:tt)*) => {{
        let _ = trap_log(format_args!($($arg)*));
    }};
}

fn trap_log(args: fmt::Arguments<'_>) -> fmt::Result {
    crate::io::console::_trap_log(args);
    Ok(())
}

pub fn init() {
    unsafe extern "C" {
        fn __alltraps();
    }

    let eentry = __alltraps as usize;

    unsafe {
        core::arch::asm!(
            "csrwr {0}, 0xc",
            in(reg) eentry,
            options(nostack)
        );
    }
    unsafe {
        core::arch::asm!(
            "csrwr $zero, 0x30",
            "csrwr $zero, 0x31",
            "csrwr $zero, 0x32",
            options(nostack)
        );
    }

    log::info!("LoongArch trap initialized: eentry={:#x}", eentry);
}



#[no_mangle]
pub extern "C" fn loongarch_trap_handler(cx: &mut TrapContext) -> &mut TrapContext {

    match cx.ecode() {
        ECODE_INT => handle_interrupt(cx),
        ECODE_SYS => {
            cx.era += 4;
            let syscall_id = cx.r[11]; // a7
            let args = [cx.r[4], cx.r[5], cx.r[6]]; // a0, a1, a2
            cx.r[4] = handle_syscall(syscall_id, args) as usize; // return in a0
        }
        ECODE_BRK => {
            trap_println!("[trap] breakpoint at era={:#x}", cx.era);
            cx.era += 4;
        }
    
        ECODE_PIL
        | ECODE_PIS
        | ECODE_PIF
        | ECODE_PME
        | ECODE_PNR
        | ECODE_PNX
        | ECODE_PPI
        | ECODE_ADEF_ADEM
        | ECODE_ALE
        | ECODE_INE
        | ECODE_IPE => {
            trap_println!(
                "[trap] fatal exception: ecode={:#x}, esubcode={:#x}, era={:#x}, badv={:#x}, estat={:#x}",
                cx.ecode(),
                cx.esubcode(),
                cx.era,
                cx.badv,
                cx.estat
            );
            panic!("fatal LoongArch exception");
        }
        _ => {
            trap_println!(
                "[trap] unsupported exception: ecode={:#x}, esubcode={:#x}, era={:#x}, badv={:#x}, estat={:#x}",
                cx.ecode(),
                cx.esubcode(),
                cx.era,
                cx.badv,
                cx.estat
            );
            panic!("unsupported LoongArch exception");
        }
    }

    cx
}

fn handle_interrupt(cx: &mut TrapContext) {
    let pending = cx.interrupt_pending_bits();

    if pending & ESTAT_IS_TIMER != 0 {
        clear_timer_interrupt();
        crate::timer::tick();
        return;
    }

    trap_println!(
        "[trap] unsupported interrupt: pending={:#x}, era={:#x}, estat={:#x}",
        pending,
        cx.era,
        cx.estat
    );
    panic!("unsupported LoongArch interrupt");
}

fn clear_timer_interrupt() {
    unsafe {
        // CSR.TICLR = 0x44, bit 0 clears the timer interrupt pending bit.
        asm!("csrwr {0}, 0x44", in(reg) 1usize, options(nostack));
    }
}

fn handle_syscall(id: usize, args: [usize; 3]) -> isize {
    trap_println!(
        "[trap] syscall id={} args=[{:#x}, {:#x}, {:#x}]",
        id,
        args[0],
        args[1],
        args[2]
    );

    crate::syscall::syscall(id, args)
}
