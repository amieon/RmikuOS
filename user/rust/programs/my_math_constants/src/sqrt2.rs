// 牛顿法求 √2，x = (x + 2/x)/2
pub fn compute_sqrt2_newton() -> u64 {
    let mut x = 1_000_000; // 初始值 1.0 放大
    let target = 2_000_000; // 2 放大 1e6
    for _ in 0..10 {
        x = (x + target / x) / 2;
    }
    x  // 近似 1414213 (√2 * 1e6)
}