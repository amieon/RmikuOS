// kernel/src/timer/mod.rs

#[cfg(target_arch = "riscv64")]
#[path = "riscv64.rs"]
mod arch_timer;

#[cfg(target_arch = "loongarch64")]
#[path = "loongarch64.rs"]
mod arch_timer;

pub use arch_timer::*;
pub use ticks;

use core::sync::atomic::{AtomicIsize, AtomicUsize, Ordering};

static TIME_OFFSET: [AtomicIsize; crate::arch::MAX_HARTS] =
    [const { AtomicIsize::new(0) }; crate::arch::MAX_HARTS];

static MONO_TIME: AtomicUsize = AtomicUsize::new(0);

pub fn monotonic_time() -> usize {
    let hart = crate::arch::hartid();
    let raw = read_arch_time();

    let adjusted = if hart < crate::arch::MAX_HARTS {
        let off = TIME_OFFSET[hart].load(Ordering::Acquire);
        raw.wrapping_add(off as usize)
    } else {
        raw
    };

    monotonic_clamp(adjusted)
}

fn monotonic_clamp(t: usize) -> usize {
    let mut old = MONO_TIME.load(Ordering::Acquire);

    loop {
        let new = if t > old { t } else { old + 1 };

        match MONO_TIME.compare_exchange(
            old,
            new,
            Ordering::AcqRel,
            Ordering::Acquire,
        ) {
            Ok(_) => return new,
            Err(v) => old = v,
        }
    }
}
pub fn calibrate_current_hart_time() {
    let hart = crate::arch::hartid();

    if hart >= crate::arch::MAX_HARTS {
        return;
    }

    if hart == 0 {
        TIME_OFFSET[0].store(0, Ordering::Release);
        return;
    }

    let master_now = MONO_TIME.load(Ordering::Acquire);
    let local_raw = read_arch_time();

    let offset = master_now as isize - local_raw as isize;
    TIME_OFFSET[hart].store(offset, Ordering::Release);
}