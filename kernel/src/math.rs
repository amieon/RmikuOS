pub fn isqrt(n: usize) -> usize {
    if n <= 1 {
        return n;
    }

    let mut x = n;
    let mut y = (x + 1) / 2;

    while y < x {
        x = y;
        y = (x + n / x) / 2;
    }

    x
}

pub fn sched_thread_scale(n: usize, alpha: isize) -> usize {
    let n = n.max(1);
    let n_sqrt: usize = isqrt(n).max(1);
    match alpha {
        0 => 1,                            // alpha = 0
        25 => isqrt(n_sqrt),               // alpha = 0.25
        50 => n_sqrt,                      // alpha = 0.5
        75 => {
            isqrt(n.saturating_mul(n_sqrt)).max(1)
        },                                 // alpha = 0.75
        100 => n,                          // alpha = 1
        _ => n_sqrt,                       // 非法值默认 sqrt
    }
}
