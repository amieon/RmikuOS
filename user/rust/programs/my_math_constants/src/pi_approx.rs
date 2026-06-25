/// π/4 = 1 - 1/3 + 1/5 - 1/7 + ...，返回放大 1_000_000 倍的整数
pub fn compute_pi_leibniz() -> u64 {
    let terms = 100_000;
    let mut sum = 0i64;
    let mut sign = 1;
    for i in 0..terms {
        let denom = 2 * i + 1;
        sum += sign * 1_000_000 / denom;
        sign = -sign;
    }
    (4 * sum) as u64
}