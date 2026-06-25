use crate::models::{User, MAX_USERS};
use crate::utils::{bytes_to_str, readline};
use ulib::io::puts;

/// 登录/注册流程，返回登录的用户索引（在 users 数组中的位置），若失败则返回 None
pub fn login_or_register(users: &mut [User; MAX_USERS], user_count: &mut usize) -> Option<usize> {
    loop {
        puts("\n========== 欢迎来到图书馆系统 ==========\n");
        puts("1. 登录\n");
        puts("2. 注册\n");
        puts("0. 退出\n");
        puts("请选择: ");
        let mut buf = [0u8; 4];
        let len = crate::utils::readline(&mut buf);
        if len == 0 { continue; }
        match buf[0] - b'0' {
            0 => return None,
            1 => {
                puts("用户名: ");
                let mut uname = [0u8; 16];
                readline(&mut uname);
                puts("密码: ");
                let mut pass = [0u8; 16];
                readline(&mut pass);
                let uname_str = bytes_to_str(&uname);
                let pass_str = bytes_to_str(&pass);
                for i in 0..*user_count {
                    if bytes_to_str(&users[i].username) == uname_str &&
                       bytes_to_str(&users[i].password) == pass_str {
                        puts("登录成功！\n");
                        return Some(i);
                    }
                }
                puts("用户名或密码错误！\n");
            }
            2 => {
                if *user_count >= MAX_USERS {
                    puts("用户已满！\n");
                    continue;
                }
                puts("用户名: ");
                let mut uname = [0u8; 16];
                readline(&mut uname);
                let uname_str = bytes_to_str(&uname);
                let mut exists = false;
                for i in 0..*user_count {
                    if bytes_to_str(&users[i].username) == uname_str {
                        exists = true;
                        break;
                    }
                }
                if exists { puts("用户名已存在！\n"); continue; }
                puts("密码: ");
                let mut pass = [0u8; 16];
                readline(&mut pass);
                puts("管理员权限？(y/n): ");
                let mut admin = [0u8; 4];
                readline(&mut admin);
                let is_admin = admin[0] == b'y' || admin[0] == b'Y';
                let id = (*user_count as u32) + 1;
                let new_user = User::new(id, uname_str, bytes_to_str(&pass), is_admin);
                users[*user_count] = new_user;   // 现在可以赋值，因为 users 是 &mut
                *user_count += 1;
                puts("注册成功！请重新登录。\n");
            }
            _ => puts("无效选项\n"),
        }
    }
}