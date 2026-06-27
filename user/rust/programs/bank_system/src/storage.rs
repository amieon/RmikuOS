use crate::models::{Account, MAX_ACCOUNTS};
use crate::utils::{bytes_to_str, atoi};
use ulib::io::{open, open_create, close, write, read, puts};
use ulib::fs::unlink;
use ulib::flag::*;

const DATA_FILE: &[u8] = b"/tmp/bank_data.txt";

pub fn save_accounts(accounts: &[Account; MAX_ACCOUNTS], count: usize) {
    let _ = unlink(DATA_FILE);
    let fd = open_create(DATA_FILE,O_RDWR);
    if fd < 0 { puts("保存账户失败\n"); return; }
    let mut buf = [0u8; 4096];
    let mut pos = 0;
    for i in 0..count {
        let len = write_account_line(&mut buf[pos..], &accounts[i]);
        pos += len;
        if pos + 256 > buf.len() {
            write(fd as usize, &buf[..pos]);
            pos = 0;
        }
    }
    if pos > 0 { write(fd as usize, &buf[..pos]); }
    close(fd as usize);
}

fn write_account_line(buf: &mut [u8], acc: &Account) -> usize {
    let name = bytes_to_str(&acc.name);
    let mut pos = 0;
    pos += write_num(buf, pos, acc.id);
    buf[pos] = b','; pos += 1;
    pos += write_str(buf, pos, name);
    buf[pos] = b','; pos += 1;
    pos += write_num(buf, pos, acc.balance);
    buf[pos] = b'\n'; pos += 1;
    pos
}

fn write_str(buf: &mut [u8], start: usize, s: &str) -> usize {
    let bytes = s.as_bytes();
    let len = if bytes.len() < buf.len() - start { bytes.len() } else { buf.len() - start - 1 };
    buf[start..start+len].copy_from_slice(&bytes[..len]);
    len
}

fn write_num(buf: &mut [u8], start: usize, mut x: u32) -> usize {
    if x == 0 { buf[start] = b'0'; return 1; }
    let mut digits = [0u8; 10];
    let mut n = 0;
    while x > 0 {
        digits[n] = b'0' + (x % 10) as u8;
        x /= 10;
        n += 1;
    }
    for i in 0..n {
        buf[start + i] = digits[n - 1 - i];
    }
    n
}

pub fn load_accounts() -> ([Account; MAX_ACCOUNTS], usize) {
    let mut accounts = [Account::new(0, "", 0); MAX_ACCOUNTS];
    let mut count = 0;
    let fd = open(DATA_FILE,O_RDWR);
    if fd < 0 { return (accounts, 0); }
    let mut buf = [0u8; 4096];
    let n = read(fd as usize, &mut buf);
    close(fd as usize);
    if n <= 0 { return (accounts, 0); }
    let mut start = 0;
    for i in 0..(n as usize) {
        if buf[i] == b'\n' {
            let line = &buf[start..i];
            if !line.is_empty() && count < MAX_ACCOUNTS {
                if let Some(acc) = parse_account_line(line) {
                    accounts[count] = acc;
                    count += 1;
                }
            }
            start = i + 1;
        }
    }
    (accounts, count)
}

fn parse_account_line(line: &[u8]) -> Option<Account> {
    let mut fields = [0usize; 3];
    let mut cnt = 0;
    let mut pos = 0;
    while pos < line.len() && cnt < 3 {
        if line[pos] == b',' { fields[cnt] = pos; cnt += 1; }
        pos += 1;
    }
    if cnt < 2 { return None; }
    let id = atoi(&line[..fields[0]]);
    let name = core::str::from_utf8(&line[fields[0]+1..fields[1]]).unwrap_or("");
    let balance = atoi(&line[fields[1]+1..line.len()]);
    Some(Account::new(id, name, balance))
}