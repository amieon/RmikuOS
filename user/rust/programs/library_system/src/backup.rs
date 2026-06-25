use ulib::process::{fork, exit, waitpid};
use ulib::io::{open, open_create, close, read, write, puts};
use ulib::fs::unlink;

const SRC: &[u8] = b"/tmp/lib_data.txt";
const DST: &[u8] = b"/tmp/lib_backup.txt";

/// 父进程调用，fork 子进程执行备份，父进程立即返回
pub fn backup_data() {
    let pid = fork();
    if pid < 0 {
        puts("备份 fork 失败\n");
        return;
    } else if pid == 0 {
        // 子进程执行备份
        puts("[备份子进程] 开始备份...\n");
        let src_fd = open(SRC);
        if src_fd < 0 {
            puts("[备份子进程] 源文件不存在\n");
            exit(1);
        }
        // 删除旧备份文件，再创建新备份
        let _ = unlink(DST);
        let dst_fd = open_create(DST);
        if dst_fd < 0 {
            puts("[备份子进程] 无法创建备份文件\n");
            close(src_fd as usize);
            exit(1);
        }
        // 一次性读取全部内容再写入（玩具做法）
        let mut buf = [0u8; 4096];
        let n = read(src_fd as usize, &mut buf);
        if n > 0 {
            if write(dst_fd as usize, &buf[..n as usize]) < 0 {
                puts("[备份子进程] 写入备份失败\n");
            } else {
                puts("[备份子进程] 备份完成！\n");
            }
        } else {
            puts("[备份子进程] 读取源文件为空\n");
        }
        close(src_fd as usize);
        close(dst_fd as usize);
        exit(0);
    }
    // 父进程不等待，子进程后台运行
}