use alloc::sync::Arc;

use super::file::{File, FileRef};
use super::stat::*;
pub struct Stdin;
pub struct Stdout;

impl File for Stdin {
    fn readable(&self) -> bool {
        true
    }

    fn writable(&self) -> bool {
        false
    }

    fn stat(&self) -> Stat {
        Stat::new(STAT_TYPE_CHAR, 0, 0o666, 0, 0)
    }

    fn read(&self, buf: &mut [u8]) -> isize {
        if buf.is_empty() {
            return 0;
        }

        let mut count = 0usize;

        while count < buf.len() {
            let mut ch = crate::io::uart::getchar_raw();
            crate::io::uart::echo_input_char(ch);
            if ch == b'\r' {
                ch = b'\n';
            }
            buf[count] = ch;
            count += 1;

            if ch == b'\n' {
                break;
            }
        }

        count as isize
    }

    fn read_nonblock(&self, buf: &mut [u8]) -> isize {
        if buf.is_empty() { return 0; }
        match crate::io::uart::try_getchar_raw() {
            Some(ch) => {
                crate::io::uart::echo_input_char(ch);
                buf[0] = if ch == b'\r' { b'\n' } else { ch };
                1
            }
            None => 0,
        }
    }

    fn write(&self, _buf: &[u8]) -> isize {
        -1
    }
}

impl File for Stdout {
    fn readable(&self) -> bool {
        false
    }

    fn writable(&self) -> bool {
        true
    }

    fn read(&self, _buf: &mut [u8]) -> isize {
        -1
    }

    fn write(&self, buf: &[u8]) -> isize {
        for &ch in buf {
            crate::io::uart::putchar_raw(ch);
        }

        buf.len() as isize
    }
    fn stat(&self) -> Stat {
        Stat::new(STAT_TYPE_CHAR, 0, 0o666, 0, 0)
    }
}

pub fn stdin() -> FileRef {
    Arc::new(Stdin)
}

pub fn stdout() -> FileRef {
    Arc::new(Stdout)
}