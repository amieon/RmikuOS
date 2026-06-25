/// e = 1 + 1/1! + 1/2! + ... ，返回放大 1_000_000 倍的整数
pub fn compute_e() -> u64 {
    let mut sum = 1_000_000u64; // 1
    let mut fact = 1u64;
    for i in 1..=20 {
        fact *= i;
        sum += 1_000_000 / fact;
    }
    sum
}