pub const SIGINT:  usize = 2;
pub const SIGILL:  usize = 4;
pub const SIGABRT: usize = 6;
pub const SIGFPE:  usize = 8;
pub const SIGKILL: usize = 9;
pub const SIGSEGV: usize = 11;
pub const SIGTERM: usize = 15;
pub const SIGCHLD: usize = 17;

/// 默认动作=终止进程的信号。SIGCHLD 不在其中: 它默认被忽略(通知类)。
pub const FATAL_SIG_MASK: u64 = 
    (1u64 << SIGINT) | (1u64 << SIGILL) | (1u64 << SIGABRT) | 
    (1u64 << SIGFPE) | (1u64 << SIGKILL) | (1u64 << SIGSEGV) | 
    (1u64 << SIGTERM);
