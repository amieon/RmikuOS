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
    //trap_println!("RISC-V trap initialized: stvec={:#x}", entry);
}

#[no_mangle]
pub extern "C" fn riscv_trap_handler(cx: &mut TrapContext) -> &mut TrapContext {
    let code = cx.cause_code();
    if cx.is_interrupt() {
        match code {
            INTERRUPT_SUPERVISOR_TIMER => {
                crate::drivers::net::on_timer_tick();
                crate::fs::rsext4fs::on_timer_tick();
                let should_schedule = crate::timer::tick();
                crate::task::account_current_tick();

                if cx.is_from_user() {

                    crate::task::do_signal(cx);

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

        // 统一出口检查:本轮若挂起了延迟调度(如抢占时锁被占),
        // 在返回用户态前补一次调度。
        if cx.is_from_user()
            && crate::task::can_preempt()
            && crate::task::check_and_clear_need_resched()
        {
            crate::task::preempt_current_and_run_next();
        }
        return cx;
    }
    match code {
        CAUSE_U_ECALL => {
            let syscall_id = cx.x[17];
            if syscall_id == crate::syscall::SYSCALL_SIGRETURN {
                /* sigreturn 不写 a0，而是把整份现场换掉（此时 sp 指向用户栈上的
                 * SigFrame）。恢复失败时内部已按 SIGSEGV 终止进程。
                 * 注意**不能**先 sepc += 4 —— 恢复后的 sepc 由 frame 决定。 */
                crate::task::sigreturn_current(cx);
            } else {
                cx.sepc += 4;
                let args = [cx.x[10], cx.x[11], cx.x[12],cx.x[13], cx.x[14], cx.x[15]];
                cx.x[10] = handle_syscall(syscall_id, args) as usize;
            }
            crate::task::do_signal(cx);
            crate::drivers::net::maybe_poll();
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
                trap_println!(
                    "[trap] SIGILL from user: sepc={:#x}, stval={:#x}, hart={}, task=?",
                    cx.sepc, cx.stval, crate::arch::current_hart_id()
                );
                crate::task::set_current_sig_pending(crate::task::SIGILL);
                crate::task::do_signal(cx);   // SIGILL 致命 -> 终止进程(exit 132)
                /* 走到这里说明 SIGILL 被 SIG_IGN 掉了。
                 * 以前这里是 panic!("SIGILL not fatal") —— 但用户态只要
                 * signal(SIGILL, SIG_IGN) 再执行一条非法指令，就能把**内核**
                 * panic 掉，正是"用户态不该炸内核"的反例。
                 * 同步异常也无法"忽略后继续"：那条指令还在原地，返回用户态
                 * 只会无限重触发（还会刷爆 115200 的串口）。所以按"无法继续"
                 * 终止进程，而不是 panic。 */
                crate::task::exit_current_and_run_next(128 + crate::task::SIGILL as i32);
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
            if cx.is_from_user() {
                /* 用户态非法内存访问 -> SIGSEGV, 只杀该进程(exit 139),
                 * 不再整个内核 panic。 */
                trap_println!(
                    "[trap] SIGSEGV from user: sepc={:#x}, stval={:#x}, hart={}",
                    cx.sepc, cx.stval, crate::arch::current_hart_id()
                );
                crate::task::set_current_sig_pending(crate::task::SIGSEGV);
                crate::task::do_signal(cx);
                panic!("SIGSEGV not fatal");  // 不应该到达
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

    // 统一出口检查:syscall 返回用户态前,补上被延迟的调度。
    if cx.is_from_user()
        && crate::task::can_preempt()
        && crate::task::check_and_clear_need_resched()
    {
        crate::task::preempt_current_and_run_next();
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
