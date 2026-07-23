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

/// 输出字符串（支持 UTF-8）
fn puts(s: &str) {
    sys_write(1, s.as_bytes());
}

/// 打印无符号 64 位整数（不换行）
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

/// 从标准输入读取一行（支持退格、回车转换），返回实际字符数（不含终止 0）
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
                // 退格擦除：光标后退、空格覆盖、再后退
                put_char(8);     // 退格
                put_char(b' ');  // 空格
                put_char(8);     // 退格
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

/// 将字节数组解析为无符号整数（遇到非数字停止）
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

// ==================== 斐波那契计算 ====================

fn fib(n: u32) -> u64 {
    if n == 0 {
        return 0;
    }
    if n == 1 {
        return 1;
    }
    let mut a = 0u64;
    let mut b = 1u64;
    for _ in 2..=n {
        let c = a + b;
        a = b;
        b = c;
    }
    b
}

// ==================== 主程序 ====================

const MAX_PROCS: usize = 20;

fn main() {
    puts("\n========== 多进程斐波那契大赛 ==========\n");
    puts("请输入斐波那契项数 n (推荐 30~45，太大可能溢出或太慢): ");

    let mut buf = [0u8; 32];
    let len = readline(&mut buf);
    let n = atoi(&buf[..len]);
    let n = if n == 0 { 35 } else { n as u32 };
    puts("计算 Fibonacci(");
    put_int(n as u64);
    puts(")\n");

    puts("请输入子进程数量 (推荐 5~10): ");
    let len = readline(&mut buf);
    let num_procs = atoi(&buf[..len]);
    let num_procs = if num_procs == 0 { 5 } else { num_procs };
    let num_procs = if num_procs > MAX_PROCS { MAX_PROCS } else { num_procs };

    puts("\n开始创建 ");
    put_int(num_procs as u64);
    puts(" 个子进程，每个都将独立计算 Fib(...)\n");

    let mut child_pids: [isize; MAX_PROCS] = [0; MAX_PROCS];
    let mut child_count = 0;

    for i in 0..num_procs {
        let pid = sys_fork();
        if pid < 0 {
            puts("fork 失败！可能内核进程表满了？\n");
            break;
        } else if pid == 0 {
            // ---------- 子进程 ----------
            let mypid = sys_getpid();

            // 部分子进程做额外休息 (i 为奇数)
            let work_ticks = (i % 3) * 2;
            if work_ticks > 0 {
                puts("[子进程 ");
                put_int(mypid as u64);
                puts("] 开始额外休息，模拟 I/O 阻塞...\n");
                sys_sleep(work_ticks);
                puts("[子进程 ");
                put_int(mypid as u64);
                puts("] 休息结束，继续计算。\n");
            }

            puts("[子进程 ");
            put_int(mypid as u64);
            puts("] 开始计算 Fibonacci(");
            put_int(n as u64);
            puts(")...\n");

            let result = fib(n);

            puts("[子进程 ");
            put_int(mypid as u64);
            puts("] 计算完成！结果是: ");
            put_int(result);
            puts("\n");

            // 退出码返回结果的低 8 位
            sys_exit((result & 0xFF) as i32);
        } else {
            // ---------- 父进程 ----------
            child_pids[child_count] = pid;
            child_count += 1;
        }
    }

    // ===== 父进程回收子进程 =====
    puts("\n=== 父进程等待子进程结束并回收 ===\n");
    let mut total_exit_sum = 0;
    let mut finished = 0;

    for i in 0..child_count {
        let mut exit_code: i32 = 0;
        let ret = sys_waitpid(child_pids[i], &mut exit_code as *mut i32);
        if ret < 0 {
            puts("waitpid 失败！子进程 ");
            put_int(child_pids[i] as u64);
            puts(" 可能已经异常？\n");
        } else {
            puts("子进程 ");
            put_int(child_pids[i] as u64);
            puts(" 已回收，退出码: ");
            put_int(exit_code as u64);
            puts("\n");
            total_exit_sum += exit_code as u64;
            finished += 1;
        }
    }

    // ===== 大赛总结 =====
    puts("\n========== 大赛总结 ==========\n");
    puts("成功回收 ");
    put_int(finished as u64);
    puts(" 个子进程（共 ");
    put_int(child_count as u64);
    puts(" 个）。\n");
    puts("所有子进程退出码之和（低8位）: ");
    put_int(total_exit_sum);
    puts("\n注意：真正的斐波那契结果已经在各子进程输出中打印。\n");
    puts("如果父进程没有卡死且正确回收了所有子进程，说明 fork/waitpid/exit 工作正常。\n");

    // 父进程自行验证
    puts("\n父进程自行验证计算 Fibonacci(");
    put_int(n as u64);
    puts(") = ");
    let parent_result = fib(n);
    put_int(parent_result);
    puts("\n");

    puts("\n按回车键退出程序...\n");
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