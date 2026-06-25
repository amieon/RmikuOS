use crate::models::Book;
use crate::utils::put_int;
use ulib::process::{fork, exit, waitpid};
use ulib::io::puts;

pub fn parallel_statistics(books: &[Book]) {
    let num_children = 3;
    let chunk_size = (books.len() + num_children - 1) / num_children;
    let mut pids = [0; 4];
    let mut count = 0;

    for i in 0..num_children {
        let start = i * chunk_size;
        let end = (start + chunk_size).min(books.len());
        if start >= books.len() { break; }
        let pid = fork();
        if pid < 0 {
            puts("统计 fork 失败\n");
            break;
        } else if pid == 0 {
            // 子进程统计自己的分段
            let mut total = 0u32;
            for j in start..end {
                total += books[j].total - books[j].available;  // 借出数
            }
            puts("[统计子进程] 分段 ");
            put_int(start as u64);
            puts(" - ");
            put_int((end - 1) as u64);
            puts(" 借出总数: ");
            put_int(total as u64);
            puts("\n");
            exit(0);
        } else {
            pids[count] = pid;
            count += 1;
        }
    }

    // 父进程等待所有子进程
    for i in 0..count {
        let mut code = 0;
        waitpid(pids[i], &mut code);
    }
    puts("并行统计完成。\n");
}