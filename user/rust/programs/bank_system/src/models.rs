use crate::utils::copy_str_to_buf;

pub const NAME_LEN: usize = 20;
pub const MAX_ACCOUNTS: usize = 30;

#[derive(Clone, Copy)]
pub struct Account {
    pub id: u32,
    pub name: [u8; NAME_LEN],
    pub balance: u32,   // 单位：分（避免浮点），例如 100 = 1.00 元
}

impl Account {
    pub fn new(id: u32, name: &str, balance: u32) -> Self {
        let mut acc = Account {
            id,
            name: [0; NAME_LEN],
            balance,
        };
        copy_str_to_buf(&mut acc.name, name);
        acc
    }
}