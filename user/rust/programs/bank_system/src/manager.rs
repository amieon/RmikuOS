use crate::models::{Account, MAX_ACCOUNTS};
use crate::storage::{save_accounts, load_accounts};
use crate::utils::{readline, atoi, bytes_to_str, put_int};
use ulib::io::{puts, put_char};

pub struct BankManager {
    pub accounts: [Account; MAX_ACCOUNTS],
    pub count: usize,
    pub next_id: u32,
}

impl BankManager {
    pub fn new() -> Self {
        let (accounts, count) = load_accounts();
        let mut max_id = 0;
        for i in 0..count {
            if accounts[i].id > max_id { max_id = accounts[i].id; }
        }
        BankManager { accounts, count, next_id: max_id + 1 }
    }

    pub fn create_account(&mut self) {
        if self.count >= MAX_ACCOUNTS { puts("账户已满\n"); return; }
        puts("户名: ");
        let mut name = [0u8; 20];
        readline(&mut name);
        puts("初始余额(分): ");
        let mut buf = [0u8; 16];
        readline(&mut buf);
        let balance = atoi(&buf);
        let id = self.next_id;
        self.next_id += 1;
        let acc = Account::new(id, bytes_to_str(&name), balance);
        self.accounts[self.count] = acc;
        self.count += 1;
        puts("创建成功！ID: "); put_int(id as u64); puts("\n");
        self.save();
    }

    pub fn deposit(&mut self) {
        puts("账户 ID: ");
        let mut buf = [0u8; 8];
        readline(&mut buf);
        let id = atoi(&buf);
        for i in 0..self.count {
            if self.accounts[i].id == id {
                puts("金额(分): ");
                let mut amt = [0u8; 16];
                readline(&mut amt);
                let money = atoi(&amt);
                self.accounts[i].balance += money;
                puts("存款成功！新余额: "); put_int(self.accounts[i].balance as u64); puts("\n");
                self.save();
                return;
            }
        }
        puts("账户不存在\n");
    }

    pub fn withdraw(&mut self) {
        puts("账户 ID: ");
        let mut buf = [0u8; 8];
        readline(&mut buf);
        let id = atoi(&buf);
        for i in 0..self.count {
            if self.accounts[i].id == id {
                puts("金额(分): ");
                let mut amt = [0u8; 16];
                readline(&mut amt);
                let money = atoi(&amt);
                if self.accounts[i].balance < money {
                    puts("余额不足！\n");
                    return;
                }
                self.accounts[i].balance -= money;
                puts("取款成功！新余额: "); put_int(self.accounts[i].balance as u64); puts("\n");
                self.save();
                return;
            }
        }
        puts("账户不存在\n");
    }

    pub fn transfer(&mut self) {
        puts("转出账户 ID: ");
        let mut buf1 = [0u8; 8];
        readline(&mut buf1);
        let from_id = atoi(&buf1);
        puts("转入账户 ID: ");
        let mut buf2 = [0u8; 8];
        readline(&mut buf2);
        let to_id = atoi(&buf2);
        puts("金额(分): ");
        let mut amt = [0u8; 16];
        readline(&mut amt);
        let money = atoi(&amt);

        let mut from_idx = None;
        let mut to_idx = None;
        for i in 0..self.count {
            if self.accounts[i].id == from_id { from_idx = Some(i); }
            if self.accounts[i].id == to_id { to_idx = Some(i); }
        }
        if from_idx.is_none() || to_idx.is_none() {
            puts("账户不存在\n");
            return;
        }
        let fi = from_idx.unwrap();
        let ti = to_idx.unwrap();
        if self.accounts[fi].balance < money {
            puts("余额不足！\n");
            return;
        }
        self.accounts[fi].balance -= money;
        self.accounts[ti].balance += money;
        puts("转账成功！\n");
        self.save();
    }

    pub fn query(&self) {
        puts("账户 ID: ");
        let mut buf = [0u8; 8];
        readline(&mut buf);
        let id = atoi(&buf);
        for i in 0..self.count {
            if self.accounts[i].id == id {
                let acc = &self.accounts[i];
                puts("ID: "); put_int(acc.id as u64);
                puts(" 户名: "); puts(bytes_to_str(&acc.name));
                puts(" 余额(分): "); put_int(acc.balance as u64);
                puts("\n");
                return;
            }
        }
        puts("账户不存在\n");
    }

    pub fn list_all(&self) {
        for i in 0..self.count {
            let acc = &self.accounts[i];
            puts("ID: "); put_int(acc.id as u64);
            puts(" 户名: "); puts(bytes_to_str(&acc.name));
            puts(" 余额: "); put_int(acc.balance as u64);
            puts("\n");
        }
    }

    pub fn save(&self) {
        save_accounts(&self.accounts, self.count);
    }
}