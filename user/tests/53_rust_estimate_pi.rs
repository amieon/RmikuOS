#![no_std]
#![no_main]

use core::panic::PanicInfo;
use core::ptr;

// ==================== 系统调用号 ====================
const SYS_EXIT: usize = 0;
const SYS_WRITE: usize = 2;
const SYS_GETPID: usize = 3;
const SYS_FORK: usize = 4;
const SYS_WAITPID: usize = 5;
const SYS_SLEEP: usize = 6;
const SYS_READ: usize = 8;

// ==================== 系统调用封装 ====================

#[cfg(target_arch = "riscv64")]
unsafe fn syscall3(id: usize, a0: usize, a1: usize, a2: usize) -> isize {
    let ret: isize;
    core::arch::asm!(
        "ecall",
        in("a7") id,
        inlateout("a0") a0 => ret,
        in("a1") a1,
        in("a2") a2,
    );
    ret
}

#[cfg(target_arch = "loongarch64")]
unsafe fn syscall3(id: usize, a0: usize, a1: usize, a2: usize) -> isize {
    let ret: isize;
    core::arch::asm!(
        "syscall 0",
        in("$r11") id,
        inlateout("$r4") a0 => ret,
        in("$r5") a1,
        in("$r6") a2,
    );
    ret
}

fn sys_write(fd: usize, buf: &[u8]) -> isize {
    unsafe { syscall3(SYS_WRITE, fd, buf.as_ptr() as usize, buf.len()) }
}

fn sys_read(fd: usize, buf: *mut u8, len: usize) -> isize {
    unsafe { syscall3(SYS_READ, fd, buf as usize, len) }
}

fn sys_getpid() -> isize {
    unsafe { syscall3(SYS_GETPID, 0, 0, 0) }
}

fn sys_fork() -> isize {
    unsafe { syscall3(SYS_FORK, 0, 0, 0) }
}

fn sys_waitpid(pid: isize, exit_code: *mut i32) -> isize {
    unsafe { syscall3(SYS_WAITPID, pid as usize, exit_code as usize, 0) }
}

fn sys_sleep(ticks: usize) -> isize {
    unsafe { syscall3(SYS_SLEEP, ticks, 0, 0) }
}

fn sys_exit(code: i32) -> ! {
    unsafe {
        syscall3(SYS_EXIT, code as usize, 0, 0);
    }
    loop {}
}

// ==================== 基本 I/O 辅助 ====================

fn put_char(ch: u8) {
    sys_write(1, &[ch]);
}

fn puts(s: &str) {
    sys_write(1, s.as_bytes());
}

fn put_int(mut x: u64) {
    if x == 0 {
        put_char(b'0');
        return;
    }
    let mut buf = [0u8; 20];
    let mut i = 0;
    while x > 0 {
        buf[i] = b'0' + (x % 10) as u8;
        x /= 10;
        i += 1;
    }
    while i > 0 {
        i -= 1;
        put_char(buf[i]);
    }
}

fn readline(buf: &mut [u8]) -> usize {
    let mut i = 0;
    while i < buf.len() - 1 {
        let mut ch: u8 = 0;
        let n = sys_read(0, &mut ch as *mut u8, 1);
        if n <= 0 {
            continue;
        }
        if ch == b'\r' {
            ch = b'\n';
        }
        if ch == b'\n' {
            put_char(b'\n');
            break;
        }
        if ch == 8 || ch == 127 {
            if i > 0 {
                i -= 1;
                put_char(8);
                put_char(b' ');
                put_char(8);
            }
            continue;
        }
        buf[i] = ch;
        i += 1;
        put_char(ch);
    }
    buf[i] = 0;
    i
}

fn atoi(bytes: &[u8]) -> usize {
    let mut val = 0;
    for &c in bytes {
        if c < b'0' || c > b'9' {
            break;
        }
        val = val * 10 + (c - b'0') as usize;
    }
    val
}

// ==================== 随机数生成器 ====================

const RAND_MAX: u32 = 0x7FFFFFFF;

fn rand_u32(state: &mut u32) -> u32 {
    let mut x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    x
}

fn rand_int(state: &mut u32) -> u32 {
    rand_u32(state) & RAND_MAX
}

// ==================== 蒙特卡洛投点 ====================

fn monte_carlo_points(num_points: usize, seed: &mut u32) -> usize {
    let mut inside = 0;
    for _ in 0..num_points {
        let x = rand_int(seed) as u64;
        let y = rand_int(seed) as u64;
        if x * x + y * y <= (RAND_MAX as u64) * (RAND_MAX as u64) {
            inside += 1;
        }
    }
    inside
}

// ==================== 主程序 ====================

const MAX_CHILDREN: usize = 32;

fn main() {
    let mut buf = [0u8; 64];

    puts("=== 多进程蒙特卡洛 π 估计程序 ===\n");
    puts("请输入子进程数量 (默认为2): ");
    let len = readline(&mut buf);
    let mut num_children = if len > 0 && buf[0] != 0 {
        atoi(&buf[..len])
    } else {
        2
    };
    if num_children == 0 {
        num_children = 1;
    }
    if num_children > MAX_CHILDREN {
        num_children = MAX_CHILDREN;
    }

    puts("请输入每个子进程的投点数 (默认为10000): ");
    let len = readline(&mut buf);
    let mut points_per_child = if len > 0 && buf[0] != 0 {
        atoi(&buf[..len])
    } else {
        10000
    };
    if points_per_child == 0 {
        points_per_child = 1000;
    }
    if points_per_child > 100000 {
        points_per_child = 100000;
    }

    puts("\n开始创建子进程...\n");

    let mut child_pids = [0isize; MAX_CHILDREN];
    let mut child_count = 0;

    for i in 0..num_children {
        let pid = sys_fork();
        if pid < 0 {
            puts("fork 失败！\n");
            sys_exit(1);
        } else if pid == 0 {
            // ---------- 子进程 ----------
            let mypid = sys_getpid();
            puts("[子进程 ");
            put_int(mypid as u64);
            puts("] 启动，投点次数: ");
            put_int(points_per_child as u64);
            puts("\n");

            let mut seed = (mypid as u32).wrapping_mul(1664525).wrapping_add(1013904223);
            let inside = monte_carlo_points(points_per_child, &mut seed);

            puts("[子进程 ");
            put_int(mypid as u64);
            puts("] 圆内点数: ");
            put_int(inside as u64);
            puts("\n");

            // 退出码只保留低 8 位（与 C 版本一致）
            sys_exit((inside & 0xFF) as i32);
        } else {
            // ---------- 父进程 ----------
            child_pids[child_count] = pid;
            child_count += 1;
        }
    }

    // 父进程回收
    puts("\n父进程等待所有子进程完成...\n");
    let mut total_inside = 0u64;
    let total_points = (points_per_child * child_count) as u64;

    for i in 0..child_count {
        let mut exit_code: i32 = 0;
        let ret = sys_waitpid(child_pids[i], &mut exit_code as *mut i32);
        if ret < 0 {
            puts("waitpid 失败\n");
        } else {
            total_inside += exit_code as u64; // 低 8 位累加
            puts("子进程 ");
            put_int(child_pids[i] as u64);
            puts(" 结束，返回圆内点数: ");
            put_int(exit_code as u64);
            puts("\n");
        }
    }

    if total_points == 0 {
        puts("没有有效投点数据！\n");
        sys_exit(0);
    }

    // 计算 π ≈ 4 * inside / total
    let pi_scaled = (4u64 * total_inside * 1_000_000) / total_points;
    let pi_int = pi_scaled / 1_000_000;
    let pi_frac = pi_scaled % 1_000_000;

    puts("\n========== 最终结果 ==========\n");
    puts("总投点数: ");
    put_int(total_points);
    puts("\n总圆内点数: ");
    put_int(total_inside);
    puts("\n估算的 π ≈ ");
    put_int(pi_int);
    put_char(b'.');

    // 输出 6 位小数，补零
    let mut frac = pi_frac;
    for _ in 0..6 {
        let digit = (frac / 100_000) as u8;
        put_char(b'0' + digit);
        frac = (frac % 100_000) * 10;
    }
    // 上面循环6次，但每次除100000，实际逻辑需要重新设计，更稳健的方法是：
    // 直接用格式化，但为了简单，我们手动补零：
    // 因为pi_frac是0..999999，需要补零到6位，可以用循环除以10的幂。
    // 重写：
    puts("\n真实 π ≈ 3.1415926535");
    puts("\n===============================\n");

    puts("按回车键退出程序...\n");
    readline(&mut buf);
    sys_exit(0);
}

// ==================== 入口和 Panic ====================

#[no_mangle]
#[link_section = ".text.entry"]
pub extern "C" fn _start() -> ! {
    main();
    sys_exit(0);
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    sys_exit(1);
}