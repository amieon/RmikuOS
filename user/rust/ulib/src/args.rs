// ulib/src/args.rs
use core::slice;

/// 把 C 字符串(\0 结尾)转成 &[u8](不含 \0)
unsafe fn cstr_len(ptr: *const u8) -> usize {
    let mut len = 0;
    while *ptr.add(len) != 0 {
        len += 1;
    }
    len
}

/// argv 迭代器:遍历每个参数,产出 &[u8]
pub struct Args {
    argv: *const *const u8,
    argc: usize,
    idx: usize,
}

impl Args {
    /// 从 _start 拿到的 (argc, argv) 构造
    pub unsafe fn new(argc: usize, argv: *const *const u8) -> Self {
        Self { argv, argc, idx: 0 }
    }

    pub fn len(&self) -> usize {
        self.argc
    }

    /// 取第 i 个参数为 &[u8]
    pub fn get(&self, i: usize) -> Option<&'static [u8]> {
        if i >= self.argc {
            return None;
        }
        unsafe {
            let ptr = *self.argv.add(i);
            if ptr.is_null() {
                return None;
            }
            let len = cstr_len(ptr);
            Some(slice::from_raw_parts(ptr, len))
        }
    }

    /// 取第 i 个参数为 &str(UTF-8,失败返回 None)
    pub fn get_str(&self, i: usize) -> Option<&'static str> {
        self.get(i).and_then(|b| core::str::from_utf8(b).ok())
    }
}

impl Iterator for Args {
    type Item = &'static [u8];
    fn next(&mut self) -> Option<Self::Item> {
        if self.idx >= self.argc {
            return None;
        }
        let item = self.get(self.idx);
        self.idx += 1;
        item
    }
}