// 计算 e（级数展开）
pub fn compute_e() -> u64 {
    let mut sum = 1_000_000u64;
    let mut fact = 1u64;
    for i in 1..=20 {
        fact *= i;
        sum += 1_000_000 / fact;
    }
    sum
}

// 计算 π（莱布尼茨级数）
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

// 计算 √2（牛顿迭代）
pub fn compute_sqrt2_newton() -> u64 {
    let mut x = 1_000_000;
    let target = 2_000_000;
    for _ in 0..10 {
        x = (x + target / x) / 2;
    }
    x
}

// 计算 φ（连分数）
pub fn compute_golden_continued() -> u64 {
    let mut frac = 1_000_000;
    for _ in 0..20 {
        frac = 1_000_000 + 1_000_000 / frac;
    }
    frac
}