use ulib::io::{read, put_char};

/// 打印无符号 64 位整数（不换行）
pub fn put_int(mut x: i32) {
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

/// 从标准输入读取一行（支持退格），返回长度
pub fn readline(buf: &mut [u8]) -> usize {
    let mut i = 0;
    while i < buf.len() - 1 {
        let mut ch = 0u8;
        let n = read(0, core::slice::from_mut(&mut ch));
        if n <= 0 { continue; }
        if ch == b'\r' { ch = b'\n'; }
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

/// 将字节切片解析为十进制整数（遇到非数字停止）
pub fn atoi(bytes: &[u8]) -> u64 {
    let mut val = 0u64;
    for &c in bytes {
        if c < b'0' || c > b'9' { break; }
        val = val * 10 + (c - b'0') as u64;
    }
    val
}

/// 生成结果文件名：/tmp/res_<pid> （如果 /tmp 不存在，可改为 b"res_"）
pub fn result_filename(pid: isize) -> [u8; 32] {
    let mut buf = [0u8; 32];
    let mut i = 0;
    let prefix = b"/tmp/res_";   // 确保 /tmp 存在，否则改为 b"res_"
    for &b in prefix {
        buf[i] = b;
        i += 1;
    }
    let mut pid_copy = pid;
    let mut digits = [0u8; 10];
    let mut d = 0;
    if pid_copy == 0 {
        digits[d] = b'0';
        d += 1;
    } else {
        while pid_copy > 0 {
            digits[d] = b'0' + (pid_copy % 10) as u8;
            pid_copy /= 10;
            d += 1;
        }
    }
    for j in (0..d).rev() {
        buf[i] = digits[j];
        i += 1;
    }
    buf[i] = 0;
    buf
}

/// 将 u64 转换为字节数组（不含终止符），返回长度
pub fn u64_to_str(mut x: u64, buf: &mut [u8]) -> usize {
    if x == 0 {
        buf[0] = b'0';
        return 1;
    }
    let mut digits = [0u8; 20];
    let mut n = 0;
    while x > 0 {
        digits[n] = b'0' + (x % 10) as u8;
        x /= 10;
        n += 1;
    }
    // 反转
    for i in 0..n {
        buf[i] = digits[n - 1 - i];
    }
    n
}