/// 信号号上限。**64 不是随手挑的**：POSIX 的 `sigset_t` 就是一个 64 位字，
/// 信号号与位图位一一对应，`1u64 << sig` 天然覆盖 1..63。想支持更多信号不是
/// "改个常量"，而是得把 sigset 换成 u128/数组 —— 全线上改。
///
/// 号段划分（与 Linux 一致）：
///   1..31   标准信号。这一段在 Linux ABI 里**已经排满**（每个号都有名字），
///           新信号不会往这里插，所以并不是"留了余量给未来"。
///   32..64  实时信号段。Linux 上 32/33 被 glibc 的 pthread 内部占掉，实际可用
///           34..64。**本内核未实现**（不排队、没有 sigqueue），但号段和位图
///           都留着位置：将来补只需要"允许 34..64 + 支持排队"，不动结构。
pub const NSIG: usize = 64;
pub const SIG_QUERY: usize = usize::MAX;


pub const SIGHUP:    usize = 1;
pub const SIGINT:    usize = 2;
pub const SIGQUIT:   usize = 3;
pub const SIGILL:    usize = 4;
pub const SIGTRAP:   usize = 5;
pub const SIGABRT:   usize = 6;
pub const SIGBUS:    usize = 7;
pub const SIGFPE:    usize = 8;
pub const SIGKILL:   usize = 9;
pub const SIGUSR1:   usize = 10;
pub const SIGSEGV:   usize = 11;
pub const SIGUSR2:   usize = 12;
pub const SIGPIPE:   usize = 13;
pub const SIGALRM:   usize = 14;
pub const SIGTERM:   usize = 15;
pub const SIGSTKFLT: usize = 16;
pub const SIGCHLD:   usize = 17;
pub const SIGCONT:   usize = 18;
pub const SIGSTOP:   usize = 19;
pub const SIGTSTP:   usize = 20;
pub const SIGTTIN:   usize = 21;
pub const SIGTTOU:   usize = 22;
pub const SIGURG:    usize = 23;
pub const SIGXCPU:   usize = 24;
pub const SIGXFSZ:   usize = 25;
pub const SIGVTALRM: usize = 26;
pub const SIGPROF:   usize = 27;
pub const SIGWINCH:  usize = 28;
pub const SIGIO:     usize = 29;
pub const SIGPWR:    usize = 30;
pub const SIGSYS:    usize = 31;

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum SigAction {
    Default,
    Ignore,
    Handler { entry: usize, restorer: usize },
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct SigFrame {
    /// 被打断那一刻的现场：通用寄存器 + 浮点 + CSR。
    pub cx: crate::trap::TrapContext,
    /// 触发本次投递的信号号（handler 的入参）
    pub signo: usize,
    /// 进入 handler 之前的屏蔽字。handler 期间会临时屏蔽自身信号
    /// （POSIX 默认行为），sigreturn 时从这里恢复。
    pub blocked: u64,
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

/// 默认动作表。**每个信号都有**默认动作（`_` 兜底为终止），但不是"每个信号
/// 一个处理函数"—— 那没有意义，能区分的只有五种行为，一张表 + 一个执行器就够。
///
/// 分类照 Linux（这也是 POSIX 的常规实现）：
///   Ign   CHLD / URG / WINCH（通知类，默认不管）
///   Cont  CONT（配合 Stop 用；没有 Stopped 状态时等于无操作，但分类要准）
///   Stop  STOP / TSTP / TTIN / TTOU
///   Core  QUIT / ILL / TRAP / ABRT / BUS / FPE / SEGV / SYS / XCPU / XFSZ
///   Term  其余（HUP / INT / KILL / PIPE / ALRM / TERM / USR1-2 / STKFLT /
///         VTALRM / PROF / IO / PWR）
pub fn default_action(sig: usize) -> DefAction {
    match sig {
        SIGCHLD | SIGURG | SIGWINCH => DefAction::Ign,
        SIGCONT => DefAction::Cont,
        SIGSTOP | SIGTSTP | SIGTTIN | SIGTTOU => DefAction::Stop,
        SIGQUIT | SIGILL | SIGTRAP | SIGABRT | SIGBUS | SIGFPE | SIGSEGV
        | SIGSYS | SIGXCPU | SIGXFSZ => DefAction::Core,
        _ => DefAction::Term,
    }
}

/// 不可被捕获/忽略/停止的信号否则进程可以变成杀不掉、停不下
pub fn unblockable(sig: usize) -> bool {
    sig == SIGKILL || sig == SIGSTOP
}
