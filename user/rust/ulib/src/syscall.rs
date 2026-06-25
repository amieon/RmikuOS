//! 原始系统调用入口:syscall3 / syscall6。
//!
//! 用 inline asm 直接触发 syscall,寄存器约定与 C 的 syscall_<arch>.S 完全一致:
//!
//!   riscv64:   号 -> a7,参数 -> a0..a5,触发 `ecall`,返回 -> a0
//!   loongarch: 号 -> r11(=a7),参数 -> r4..r9(=a0..a5),触发 `syscall 0`,返回 -> r4(=a0)
//!
//! 这是整个 ulib 唯一与架构相关的文件,用 cfg 选择对应实现。

// ============================ riscv64 ============================

#[cfg(target_arch = "riscv64")]
#[inline]
pub unsafe fn syscall3(id: usize, a0: usize, a1: usize, a2: usize) -> isize {
    let ret: isize;
    core::arch::asm!(
        "ecall",
        in("a7") id,
        inlateout("a0") a0 => ret,
        in("a1") a1,
        in("a2") a2,
        options(nostack),
    );
    ret
}

#[cfg(target_arch = "riscv64")]
#[inline]
pub unsafe fn syscall6(
    id: usize,
    a0: usize, a1: usize, a2: usize,
    a3: usize, a4: usize, a5: usize,
) -> isize {
    let ret: isize;
    core::arch::asm!(
        "ecall",
        in("a7") id,
        inlateout("a0") a0 => ret,
        in("a1") a1,
        in("a2") a2,
        in("a3") a3,
        in("a4") a4,
        in("a5") a5,
        options(nostack),
    );
    ret
}

// ========================== loongarch64 ==========================
//
// 注意:loongarch 的 Rust inline asm 寄存器命名可能是 `$r4`/`$r11`,
// 也可能要求用 ABI 名 `$a0`/`$a7`,取决于工具链版本。若编译报
// "invalid register" / "unknown register",把下面的 `$r4`/`$r5`/.../`$r11`
// 改成对应的 `$a0`/`$a1`/.../`$a7` 再试。

#[cfg(target_arch = "loongarch64")]
#[inline]
pub unsafe fn syscall3(id: usize, a0: usize, a1: usize, a2: usize) -> isize {
    let ret: isize;
    core::arch::asm!(
        "syscall 0",
        in("$r11") id,
        inlateout("$r4") a0 => ret,
        in("$r5") a1,
        in("$r6") a2,
        options(nostack),
    );
    ret
}

#[cfg(target_arch = "loongarch64")]
#[inline]
pub unsafe fn syscall6(
    id: usize,
    a0: usize, a1: usize, a2: usize,
    a3: usize, a4: usize, a5: usize,
) -> isize {
    let ret: isize;
    core::arch::asm!(
        "syscall 0",
        in("$r11") id,
        inlateout("$r4") a0 => ret,
        in("$r5") a1,
        in("$r6") a2,
        in("$r7") a3,
        in("$r8") a4,
        in("$r9") a5,
        options(nostack),
    );
    ret
}
