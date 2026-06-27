#![no_std]
#![no_main]

mod constants;
mod utils;

use constants::{
    compute_e, compute_pi_leibniz,
    compute_sqrt2_newton, compute_golden_continued,
};
use utils::{put_int, readline, atoi, result_filename, u64_to_str};

use ulib::io::{puts, open, open_create, close, read, write};
use ulib::fs::unlink;
use ulib::flag::*;
use ulib::process::{fork, waitpid, exit, getpid};

#[no_mangle]
#[link_section = ".text.entry"]
pub extern "C" fn _start() -> ! {
    puts("\n========== 多进程数学常数大冒险 ==========\n");
    puts("本程序将并行计算 4 个著名常数：\n");
    puts("  e (欧拉数)      —— 使用级数展开\n");
    puts("  π (圆周率)      —— 使用莱布尼茨级数\n");
    puts("  √2              —— 使用牛顿迭代\n");
    puts("  φ (黄金比例)    —— 使用连分数\n");
    puts("每个常数由独立的子进程计算，结果写入临时文件，父进程读取汇总。\n\n");

    let tasks = [
        ("e", compute_e as fn() -> u64),
        ("π", compute_pi_leibniz),
        ("√2", compute_sqrt2_newton),
        ("φ", compute_golden_continued),
    ];

    let mut child_pids = [0isize; 4];
    let mut child_count = 0;

    for (idx, (name, func)) in tasks.iter().enumerate() {
        let pid = fork();
        if pid < 0 {
            puts("fork 失败！\n");
            break;
        } else if pid == 0 {
            // ---------- 子进程 ----------
            let mypid = getpid();
            puts("[子进程 ");
            put_int(mypid as u64);
            puts("] 开始计算常数 ");
            puts(name);
            puts(" ...\n");

            let result = func();
            
            // 将结果写入临时文件
            let fname = result_filename(mypid);
            let fd = open_create(&fname,O_RDWR);
            if fd >= 0 {
                let mut buf = [0u8; 32];
                let len = u64_to_str(result, &mut buf);
                let written = write(fd as usize, &buf[..len]);
                if written < 0 {
                    puts("[子进程] 写入文件失败\n");
                }
                close(fd as usize);
                puts("[子进程 ");
                put_int(mypid as u64);
                puts("] 结果已写入文件\n");
            } else {
                puts("[子进程 ");
                put_int(mypid as u64);
                puts("] 无法创建结果文件！\n");
            }
            exit(0);
        } else {
            child_pids[child_count] = pid;
            child_count += 1;
        }
    }

    // 父进程等待并读取结果
    puts("\n父进程等待所有子进程结束...\n");
    let mut final_results = [0u64; 4];
    let mut success_count = 0;

    for i in 0..child_count {
        let mut exit_code = 0;
        let ret = waitpid(child_pids[i], &mut exit_code);
        if ret < 0 {
            puts("waitpid 失败！\n");
            continue;
        }
        let fname = result_filename(child_pids[i]);
        let fd = open(&fname,O_RDWR);
        if fd >= 0 {
            let mut buf = [0u8; 32];
            let n = read(fd as usize, &mut buf);
            if n > 0 {
                let val = atoi(&buf[..n as usize]);
                final_results[i] = val;
                success_count += 1;
                puts("子进程 ");
                put_int(child_pids[i] as u64);
                puts(" 结果: ");
                put_int(val);
                puts("\n");
            } else {
                puts("读取结果文件为空！\n");
            }
            close(fd as usize);
            if unlink(&fname) < 0 {
                puts("删除临时文件失败\n");
            }
        } else {
            puts("无法打开结果文件！\n");
        }
    }

    // 汇总
    puts("\n========== 计算结果汇总 ==========\n");
    let names = ["e", "π", "√2", "φ"];
    for i in 0..success_count {
        puts(names[i]);
        puts(" = ");
        put_int(final_results[i]);
        puts("\n");
    }
    puts("成功回收 ");
    put_int(success_count as u64);
    puts(" 个子进程。\n");
    puts("\n按回车键退出程序...\n");
    let _ = readline(&mut [0u8; 1]);
    exit(0);
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    exit(1);
}