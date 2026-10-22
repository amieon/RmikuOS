pub const SIGHUP:   usize = 1;
pub const SIGINT:   usize = 2;
pub const SIGQUIT:  usize = 3;
pub const SIGILL:   usize = 4;
pub const SIGTRAP:  usize = 5;
pub const SIGABRT:  usize = 6;
pub const SIGBUS:   usize = 7;
pub const SIGFPE:   usize = 8;
pub const SIGKILL:  usize = 9;
pub const SIGUSR1:  usize = 10;
pub const SIGSEGV:  usize = 11;
pub const SIGUSR2:  usize = 12;
pub const SIGPIPE:  usize = 13;
pub const SIGALRM:  usize = 14;
pub const SIGTERM:  usize = 15;
pub const SIGCHLD:  usize = 17;
pub const SIGCONT:  usize = 18;
pub const SIGSTOP:  usize = 19;
pub const SIGTSTP:  usize = 20;
pub const SIGTTIN:  usize = 21;
pub const SIGTTOU:  usize = 22;
pub const SIGURG:   usize = 23;
pub const SIGWINCH: usize = 28;
pub const SIGSYS:   usize = 31;

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum SigAction {
    Default,
    Ignore,
    Handler { entry: usize, restorer: usize },
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct SigFrame {
    /// 被打断那一刻的完整现场：32 个通用寄存器 + 32 个浮点 + CSR。
    pub cx: crate::trap::TrapContext,
    /// 触发本次投递的信号号（handler 的入参）
    pub signo: usize,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum DefAction {
    Term,
    /// 终止并留下 core dump（`WIFSIGNALED` 状态里的 bit7）
    Core,
    Ign,
    Stop,
    Cont,
}

pub fn default_action(sig: usize) -> DefAction {
    match sig {
        SIGCHLD | SIGURG | SIGWINCH | SIGCONT => DefAction::Ign,
        SIGSTOP | SIGTSTP | SIGTTIN | SIGTTOU => DefAction::Stop,
        SIGQUIT | SIGILL | SIGABRT | SIGFPE | SIGSEGV | SIGBUS => DefAction::Core,
        _ => DefAction::Term,
    }
}

/// 不可被捕获/忽略/停止的信号否则进程可以变成杀不掉、停不下
pub fn unblockable(sig: usize) -> bool {
    sig == SIGKILL || sig == SIGSTOP
}
