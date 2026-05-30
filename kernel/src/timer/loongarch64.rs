use core::arch::asm;
use core::sync::atomic::{AtomicUsize, Ordering};

static TICKS: AtomicUsize = AtomicUsize::new(0);

/*
 * 先调小一点，方便验证抢占。
 * 如果日志太多或者切太快，再调大。
 */
const TIMER_INITVAL: usize = 20_000;

const TICKS_PER_SLICE: usize = 1;

pub fn init() {
    unsafe {
        /*
         * enable local timer interrupt: ECFG.LIE[11]
         */
        asm!(
            "csrrd  $t0, 0x4",
            "ori    $t0, $t0, 0x800",
            "csrwr  $t0, 0x4",
            options(nostack)
        );

        /*
         * TCFG:
         *   bit0 = En
         *   bit1 = Periodic
         *   bits[n-1:2] = InitVal
         */
        let tcfg = (TIMER_INITVAL << 2) | 0b11;
        asm!("csrwr {0}, 0x41", in(reg) tcfg, options(nostack));

        /*
         * enable global interrupt: CRMD.IE
         */
        asm!(
            "csrrd  $t0, 0x0",
            "ori    $t0, $t0, 0x4",
            "csrwr  $t0, 0x0",
            options(nostack)
        );
    }
}


pub fn tick() -> bool {

    clear_timer_interrupt();

    let n = TICKS.fetch_add(1, Ordering::Relaxed) + 1;

    

    n % TICKS_PER_SLICE == 0
}
pub fn ticks() -> usize {
    TICKS.load(Ordering::Relaxed)
}

fn clear_timer_interrupt() {
    unsafe {
        // CSR.TICLR = 0x44, bit 0 clears timer interrupt pending bit.
        asm!("csrwr {0}, 0x44", in(reg) 1usize, options(nostack));
    }
}