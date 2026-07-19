//! RISC-V trap handling.
//!
//! This is a kernel bring-up trap framework, not rCore's batch-app framework.
//! It does not call run_next_app(); unsupported traps are logged and then panic.

mod context;

use core::arch::{asm, global_asm};
use core::fmt::{self, Write};

pub use context::TrapContext;

use crate::println;

global_asm!(include_str!("trap.S"));



const CAUSE_U_ECALL: usize = 8;
const CAUSE_S_ECALL: usize = 9;
const CAUSE_BREAKPOINT: usize = 3;
const CAUSE_ILLEGAL_INSTRUCTION: usize = 2;
const CAUSE_LOAD_FAULT: usize = 5;
const CAUSE_STORE_FAULT: usize = 7;
const CAUSE_INST_PAGE_FAULT: usize = 12;
const CAUSE_LOAD_PAGE_FAULT: usize = 13;
const CAUSE_STORE_PAGE_FAULT: usize = 15;

const INTERRUPT_SUPERVISOR_TIMER: usize = 5;
const INTERRUPT_SUPERVISOR_SOFT: usize = 1; 


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

    let entry = __alltraps as usize;
    unsafe {
        asm!("csrw stvec, {0}", in(reg) entry, options(nostack));
    }
    unsafe {
        asm!("csrw sscratch, zero", options(nostack));
    }
    trap_println!("RISC-V trap initialized: stvec={:#x}", entry);
}

#[no_mangle]
pub extern "C" fn riscv_trap_handler(cx: &mut TrapContext) -> &mut TrapContext {
    let code = cx.cause_code();
    if cx.is_interrupt() {
        match code {
            INTERRUPT_SUPERVISOR_TIMER => {
                crate::drivers::net::poll();
                let should_schedule = crate::timer::tick();

                if cx.is_from_user() {
                    crate::task::account_current_tick();
                    crate::task::do_signal();

                    if should_schedule {
                        if crate::task::can_preempt() {
                            crate::task::preempt_current_and_run_next();
                        } else {
                            crate::task::set_current_need_resched(true);
                        }
                    }
                }
            }
            INTERRUPT_SUPERVISOR_SOFT => {
                crate::arch::ipi::clear_soft_interrupt();

                let need_resched = crate::arch::ipi::handle_ipi();

                if need_resched {
                    if cx.is_from_user() {
                        if crate::task::can_preempt() {
                            crate::task::preempt_current_and_run_next();
                        } else {
                            crate::task::set_current_need_resched(true);
                        }
                    } else {
                        // scheduler/idle/kernel context：不要直接切任务
                        crate::task::set_current_need_resched(true);
                    }
                }
            }
            _ => {
                trap_println!(
                    "[trap] unsupported interrupt: scause={:#x}, sepc={:#x}, stval={:#x}",
                    cx.scause,
                    cx.sepc,
                    cx.stval
                );
                panic!("unsupported RISC-V interrupt");
            }
        }
        return cx;
    }
    match code {
        CAUSE_U_ECALL => {
            cx.sepc += 4;
            let syscall_id = cx.x[17];
            let args = [cx.x[10], cx.x[11], cx.x[12],cx.x[13], cx.x[14], cx.x[15]];
            cx.x[10] = handle_syscall(syscall_id, args) as usize;
            crate::task::do_signal();
        }

        CAUSE_S_ECALL => {
            trap_println!("[trap] unexpected supervisor ecall at sepc={:#x}", cx.sepc);
            panic!("unexpected supervisor ecall");
        }
        CAUSE_BREAKPOINT => {
            trap_println!("[trap] breakpoint at sepc={:#x}", cx.sepc);
            // This assumes the normal 32-bit ebreak instruction.  If you later
            // enable and use compressed c.ebreak, decode instruction length here.
            cx.sepc += 4;
        }
        CAUSE_ILLEGAL_INSTRUCTION => {
            
            if cx.is_from_user() {
                crate::task::set_current_sig_pending(crate::task::SIGILL);
                panic!("SIGILL not fatal");  // 不应该到达
            } else {
                trap_println!(
                    "[trap] fatal exception: code={}, sepc={:#x}, stval={:#x}, scause={:#x}",
                    code,
                    cx.sepc,
                    cx.stval,
                    cx.scause
                );
                panic!("fatal RISC-V exception");
            }
            
        }
        CAUSE_LOAD_FAULT
        | CAUSE_STORE_FAULT
        | CAUSE_INST_PAGE_FAULT
        | CAUSE_LOAD_PAGE_FAULT
        | CAUSE_STORE_PAGE_FAULT => {
            trap_println!(
                "[trap] fatal exception: code={}, sepc={:#x}, stval={:#x}, scause={:#x}",
                code,
                cx.sepc,
                cx.stval,
                cx.scause
            );
            panic!("fatal RISC-V exception");
        }
        _ => {
            trap_println!(
                "[trap] unsupported exception: code={}, sepc={:#x}, stval={:#x}, scause={:#x}",
                code,
                cx.sepc,
                cx.stval,
                cx.scause
            );
            panic!("unsupported RISC-V exception");
        }
    }

    cx
}

fn handle_syscall(id: usize, args: [usize; 6]) -> isize {
    
    // trap_println!(
    //     "[trap] syscall id={} args=[{:#x}, {:#x}, {:#x}]",
    //     id,
    //     args[0],
    //     args[1],
    //     args[2]
    // );
    crate::syscall::syscall(id, args)
}
