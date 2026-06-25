// φ = 1 + 1/(1+1/(1+...))
pub fn compute_golden_continued() -> u64 {
    let mut frac = 1_000_000; // 初始 1.0
    for _ in 0..20 {
        frac = 1_000_000 + 1_000_000 / frac;
    }
    frac  // 约 1618033 (φ * 1e6)
}