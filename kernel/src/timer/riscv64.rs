use core::arch::asm;
use core::sync::atomic::{AtomicUsize, Ordering};

static TICKS: AtomicUsize = AtomicUsize::new(0);

/*
 * QEMU 上 10_000_000 可能太慢，抢占不明显。
 * 先用小一点，确认成功后再调大。
 */
const INTERVAL: usize = 200_000;

/*
 * 每多少次 timer interrupt 调度一次。
 * 调试阶段可以设成 1，方便看到抢占。
 */
const TICKS_PER_SLICE: usize = 1;

pub fn init() {
    set_next_timer();

    unsafe {
        // enable supervisor timer interrupt: sie.STIE
        asm!("csrs sie, {}", in(reg) 1usize << 5, options(nostack));

        // enable supervisor global interrupt: sstatus.SIE
        asm!("csrs sstatus, {}", in(reg) 1usize << 1, options(nostack));
    }
}

/*
 * 返回 true 表示这次 tick 应该触发任务调度。
 */
pub fn tick() -> bool {
    let n = TICKS.fetch_add(1, Ordering::Relaxed) + 1;

    /*
     * RISC-V timer 是一次性的，所以每次中断都要设置下一次。
     */
    set_next_timer();

    n % TICKS_PER_SLICE == 0
}

fn read_time() -> usize {
    let time: usize;
    unsafe {
        asm!("csrr {}, time", out(reg) time, options(nostack));
    }
    time
}

fn set_next_timer() {
    sbi_set_timer(read_time() + INTERVAL);
}

fn sbi_set_timer(stime_value: usize) {
    unsafe {
        asm!(
            "ecall",
            in("a0") stime_value,
            in("a6") 0usize,          // FID: set_timer
            in("a7") 0x54494D45usize, // EID: TIME
            lateout("a0") _,
            lateout("a1") _,
        );
    }
}

pub fn ticks() -> usize {
    TICKS.load(Ordering::Relaxed)
}