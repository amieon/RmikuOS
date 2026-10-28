use alloc::sync::Arc;

use super::file::{File, FileRef};
use super::stat::*;

pub struct NullFile;

impl File for NullFile {
    fn readable(&self) -> bool {
        true
    }

    fn writable(&self) -> bool {
        true
    }

    
    fn read(&self, _buf: &mut [u8]) -> isize {
        0
    }

    
    fn write(&self, buf: &[u8]) -> isize {
        buf.len() as isize
    }

    fn stat(&self) -> Stat {
        Stat::new(STAT_TYPE_CHAR, 0, 0o666, 0, 0).with_dev(DEV_NULL)
    }
}

pub fn null() -> FileRef {
    Arc::new(NullFile)
}

/// 绝对路径 → 内核内置设备节点（不是真文件系统里的东西）。
///
/// 只列真正实现了的：`/dev/zero`、`/dev/tty` 都还没有，宁可让 open 报
/// ENOENT，也不返回一个行为不对的东西。
pub fn builtin_device(abs: &str) -> Option<FileRef> {
    match abs {
        "/dev/null" => Some(null()),
        _ => None,
    }
}
