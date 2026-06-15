// math.rs —— 连续 alpha 的就绪线程缩放因子（纯整数，无浮点）。
//
// 语义：返回 floor( n ^ (alpha/100) )，alpha ∈ [0, 100]。
//   alpha=0   -> 1        (n^0)
//   alpha=50  -> floor(√n)
//   alpha=100 -> n        (n^1)
// 中间任意整数 alpha 都给出平滑、单调不降的结果，
// 全程精度 ≥ 旧的五档 match 实现（误差始终 < 1，且为真值下取整）。
//
// 实现思路：n^e（e=alpha/100 ∈ (0,1)）的指数 e 写成二进制小数
//   e = Σ b_k · 2^-k
// 则 n^e = Π (n^(2^-k))^{b_k}，而 n^(2^-k) 就是对 n 连续开 k 次方。
// 用定点数（基数 SCALE）承载这些分数次幂，逐位累乘即可。纯 isqrt，无浮点。

/// 整数平方根（Newton 迭代）。
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

/// 定点基数。1<<20 给约 1e-6 的小数分辨率，
/// 配合 saturating 乘法，usize(64位) 下不会溢出（见下方说明）。
const SCALE: usize = 1 << 20;

/// 定点开方：输入输出都按 SCALE 缩放。
/// 设 real = x_fp / SCALE，要返回 sqrt(real) * SCALE = isqrt(x_fp * SCALE)。
///
/// 溢出说明：x_fp 最大约 n * SCALE。x_fp * SCALE ≈ n * SCALE^2 = n * 2^40。
/// 只要 n < 2^24（一千六百万就绪线程，远超任何真实场景），就不会超过 2^64。
/// 这里再 saturating 兜一层底。
fn sqrt_fp(x_fp: usize) -> usize {
    isqrt(x_fp.saturating_mul(SCALE))
}

/// 连续版 n^(alpha/100)。alpha 会被 clamp 到 [0,100]，内核侧不信任调用方。
pub fn sched_thread_scale(n: usize, alpha: isize) -> usize {
    let n = n.max(1);

    let alpha = alpha.clamp(0, 100) as usize;

    // 锚点快路径，省去循环（也保证端点精确）。
    if alpha == 0 {
        return 1; // n^0
    }
    if alpha == 100 {
        return n; // n^1
    }

    // 维护：
    //   acc = 累积的 n^(已处理分数位)，定点
    //   cur = n^(2^-k)，定点，每轮再开一次方让 k 增大
    let mut acc: usize = SCALE; // 定点 1.0
    let mut cur: usize = sqrt_fp(n.saturating_mul(SCALE)); // n^(1/2)

    // 把 e = alpha/100 按二进制小数逐位取出（“乘 2 取整”法）。
    let mut e_num = alpha; // 分子
    const E_DEN: usize = 100; // 分母
    const BITS: usize = 20; // 处理的最大小数位数

    let mut i = 0;
    while i < BITS {
        e_num *= 2;
        let bit = e_num / E_DEN;
        e_num %= E_DEN;

        if bit == 1 {
            // 命中该位：acc *= cur（定点乘后缩回）
            acc = acc.saturating_mul(cur) / SCALE;
        }

        // 下一位：指数再 /2，对应再开一次方
        cur = sqrt_fp(cur);

        // e 已精确表示完毕，提前结束
        if e_num == 0 {
            break;
        }

        i += 1;
    }

    (acc / SCALE).max(1)
}

#[cfg(test)]
mod tests {
    use super::*;

    // 旧五档实现，用于回归对照。
    fn scale_orig(n: usize, alpha: isize) -> usize {
        let n = n.max(1);
        let ns = isqrt(n).max(1);
        match alpha {
            0 => 1,
            25 => isqrt(ns),
            50 => ns,
            75 => isqrt(n.saturating_mul(ns)).max(1),
            100 => n,
            _ => ns,
        }
    }

    #[test]
    fn endpoints_exact() {
        for n in 1..200 {
            assert_eq!(sched_thread_scale(n, 0), 1);
            assert_eq!(sched_thread_scale(n, 100), n);
        }
    }

    #[test]
    fn never_exceeds_n_and_at_least_one() {
        for n in 1..500 {
            for a in 0..=100 {
                let v = sched_thread_scale(n, a as isize);
                assert!(v >= 1);
                assert!(v <= n, "n={n} a={a} v={v} > n");
            }
        }
    }

    #[test]
    fn monotonic_in_alpha() {
        for n in 1..200 {
            let mut prev = 0;
            for a in 0..=100 {
                let v = sched_thread_scale(n, a as isize);
                assert!(v >= prev, "regress n={n} a={a}: {prev}->{v}");
                prev = v;
            }
        }
    }

    #[test]
    fn alpha_50_is_isqrt() {
        for n in 1..1000 {
            assert_eq!(sched_thread_scale(n, 50), isqrt(n).max(1));
        }
    }

    #[test]
    fn clamps_out_of_range() {
        // 负数与 >100 都被夹回端点
        assert_eq!(sched_thread_scale(50, -10), 1);
        assert_eq!(sched_thread_scale(50, 200), 50);
    }

    #[test]
    fn at_least_as_accurate_as_orig_at_anchors() {
        // 新版在锚点处精度不低于旧版（与真值 floor 比较）
        for n in 2..200 {
            for a in [25isize, 50, 75] {
                let v = sched_thread_scale(n, a);
                let e = (a as f64) / 100.0;
                let truth = (n as f64).powf(e);
                let err_new = (v as f64 - truth).abs();
                let err_old = (scale_orig(n, a) as f64 - truth).abs();
                assert!(err_new <= err_old + 1e-9, "n={n} a={a}");
            }
        }
    }
}