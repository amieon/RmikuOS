use ulib::process::{fork, exit};
use ulib::io::{open, open_create, close, read, write, puts};
use ulib::fs::unlink;

pub fn backup_data() {
    let pid = fork();
    if pid < 0 {
        puts("备份 fork 失败\n");
        return;
    } else if pid == 0 {
        puts("[备份子进程] 开始备份...\n");
        let src = b"/tmp/books.txt\0";
        let dst = b"/tmp/books_backup.txt\0";
        let src_fd = open(src);
        if src_fd < 0 { puts("源文件不存在\n"); exit(1); }
        let _ = unlink(dst);
        let dst_fd = open_create(dst);
        if dst_fd < 0 { puts("创建备份失败\n"); close(src_fd as usize); exit(1); }
        let mut buf = [0u8; 4096];
        let n = read(src_fd as usize, &mut buf);
        if n > 0 { write(dst_fd as usize, &buf[..n as usize]); }
        close(src_fd as usize);
        close(dst_fd as usize);
        puts("[备份子进程] 备份完成！\n");
        exit(0);
    }
}