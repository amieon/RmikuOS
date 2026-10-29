use alloc::string::String;
use alloc::vec::Vec;

use crate::fs::FileRef;
use crate::mm::{MemorySet, PhysPageNum};

use super::thread::Tid;
use super::SigAction;

pub type Pid = usize;

pub const DEFAULT_TICKETS: usize = 100;
pub const BIG_STRIDE: usize = 10_000_000;

/// 附加组(supplementary groups)的最大数量(POSIX NGROUPS_MAX 通常取 32)。
pub const NGROUPS_MAX: usize = 32;

pub fn stride_from_tickets(tickets: usize) -> usize {
    let tickets = tickets.max(1);
    (BIG_STRIDE / tickets).max(1)
}

pub struct ProcessControlBlock {
    pub pid: Pid,

    pub parent: Option<Pid>,
    pub children: Vec<Pid>,

    pub user_space: MemorySet,

    pub fd_table: Vec<Option<FileRef>>,
    pub fd_flags: Vec<usize>,
    pub fd_cloexec: Vec<bool>,
    pub free_fds: Vec<usize>,
    pub cwd: String,

    pub env: Vec<(String, String)>,

    pub threads: Vec<Tid>,
    pub ready_threads: Vec<Tid>,

    pub tickets: usize,
    pub stride: usize,
    pub pass: usize,

    pub run_ticks: usize,
    pub effective_tickets: usize,
    pub ready_thread_count_snapshot: usize,

    /// 可运行(Ready+Running)线程数缓存:由状态变迁点增量维护,
    /// 供 pick 热路径 O(1) 读取,不再全表重扫。
    pub runnable_count: usize,

    pub mmap_areas: Vec<MmapArea>,
    pub mmap_free_ranges: Vec<MmapFreeRange>,
    pub mmap_next: usize,

    pub sig_pending: u64,
    pub sig_actions: [SigAction; super::NSIG],

    /// 进程凭证(POSIX uid/euid/gid/egid)。0 = root / 超级用户。
    pub uid: usize,
    pub euid: usize,
    pub gid: usize,
    pub egid: usize,

    /// 附加组(supplementary groups)。groups[..ngroups] 为有效组列表,
    /// 用于文件访问检查时与文件属组匹配(任一相等即视为"组"权限)。
    pub groups: [usize; NGROUPS_MAX],
    pub ngroups: usize,

    /// 文件创建掩码（POSIX umask）：新建文件/目录时按 `mode & !umask` 收窄权限。
    /// 由 fork 继承、exec 保持不变（与 POSIX 一致）。默认 022。
    pub umask: u16,

    /// 进程组号
    pub pgid: usize,

    /// 会话号
    pub sid: usize,

    pub exit_code: i32,

    /// 致死信号号（0 = 进程自己 `exit()` 正常退出）。
    pub term_sig: usize,

    /// 是否以"core dump 类"信号死亡（SIGSEGV/SIGABRT/...）。`waitpid` 的
    /// wait status bit7（`WCOREDUMP`）就取自这里。内核本身不写 core 文件。
    pub coredump: bool,

    /// 让本进程停住的信号号（0 = 未停止）。`waitpid(WUNTRACED)` 用它填
    /// `WSTOPSIG`。停止是进程级的（POSIX：一个线程被停，整个进程停）。
    pub stop_sig: usize,
    /// 这次停止事件是否已报给父进程。报过就不再重复报，直到下一次停止 ——
    /// 否则父进程每轮 waitpid 都会看到"又停了"。
    pub stop_reported: bool,
    /// 曾经停止、之后被 SIGCONT 继续过、尚未报给父进程（`WCONTINUED`）。
    pub continued: bool,

    /// `ITIMER_REAL` 的下次到期时刻（单调微秒，0 = 未启动）。到点发 SIGALRM。
    /// 用微秒而不是 tick：`setitimer` 的参数就是微秒，而两个架构的 tick
    /// 频率不同（见 timer/mod.rs），存 tick 就得处处换算。
    pub itimer_next_us: u64,
    /// `ITIMER_REAL` 的周期间隔（微秒，0 = 只响一次）。
    pub itimer_interval_us: u64,
}

impl ProcessControlBlock {
    pub fn new(
        pid: Pid,
        user_space: MemorySet,
        cwd: String,
    ) -> Self {
        Self {
            pid,

            parent: None,
            children: Vec::new(),

            user_space,

            fd_table: Self::new_fd_table(),
            free_fds: Vec::new(),
            fd_flags: Vec::new(),
            fd_cloexec: Vec::new(),
            cwd,

            env: Vec::new(),

            threads: Vec::new(),
            ready_threads: Vec::new(),

            tickets: DEFAULT_TICKETS,
            stride: BIG_STRIDE / DEFAULT_TICKETS,
            pass: 0,

            run_ticks: 0,
            effective_tickets: DEFAULT_TICKETS,
            ready_thread_count_snapshot: 0,
            runnable_count: 0,
            
            mmap_areas: Vec::new(),
            mmap_free_ranges: Vec::new(),
            mmap_next: USER_MMAP_BASE,

            sig_pending: 0,
            sig_actions: [SigAction::Default; super::NSIG],

            // 默认 root（uid/euid/gid/egid = 0）。init 进程即以此身份启动。
            uid: 0,
            euid: 0,
            gid: 0,
            egid: 0,

            // 默认无附加组。
            groups: [0; NGROUPS_MAX],
            ngroups: 0,

            // 默认 022：新建文件 0666→0644、目录 0777→0755，与常规发行版一致。
            umask: 0o022,

            // 新进程自成一组：组号就是自己的 pid。
            pgid: pid,
            // 同理自成一会话。
            sid: pid,

            exit_code: 0,
            term_sig: 0,
            coredump: false,
            stop_sig: 0,
            stop_reported: false,
            continued: false,
            /* 定时器不跨 fork 继承（POSIX）：子进程从"未启动"开始。
             * exec 则保留，所以这里只在两个构造点初始化。 */
            itimer_next_us: 0,
            itimer_interval_us: 0,
        }
    }

    pub fn fork_from(
        pid: Pid,
        parent: Pid,
        user_space: MemorySet,
        fd_table: Vec<Option<FileRef>>,
        fd_flags: Vec<usize>,
        fd_cloexec: Vec<bool>,
        free_fds: Vec<usize>,
        cwd: String,
        env: Vec<(String, String)>,
        parent_tickets: usize,
        parent_pass: usize,
        mmap_areas: Vec<MmapArea>,
        mmap_free_ranges: Vec<MmapFreeRange>,
        mmap_next: usize,
        uid: usize,
        euid: usize,
        gid: usize,
        egid: usize,
        groups: [usize; NGROUPS_MAX],
        ngroups: usize,
        umask: u16,
        pgid: usize,
        sid: usize,
    ) -> Self {
        let tickets = parent_tickets.max(1);
        let stride = BIG_STRIDE / tickets;

        Self {
            pid,

            parent: Some(parent),
            children: Vec::new(),

            user_space,

            fd_table,
            fd_flags,
            fd_cloexec,
            free_fds,
            cwd,

            env,

            threads: Vec::new(),
            ready_threads: Vec::new(),

            tickets,
            stride,
            pass: parent_pass,

            run_ticks: 0,
            effective_tickets: tickets,
            ready_thread_count_snapshot: 1,
            runnable_count: 1,

            mmap_areas,
            mmap_free_ranges,
            mmap_next,

            sig_pending: 0,
            /* 处置表与屏蔽字都不继承：POSIX 是继承的，但 shell 自己
             * signal(SIGINT, SIG_IGN)，继承会让子进程也忽略 Ctrl+C。
             * 前提条件见 sig_actions 字段的注释。 */
            sig_actions: [SigAction::Default; super::NSIG],

            uid,
            euid,
            gid,
            egid,

            groups,
            ngroups,
            umask,
            pgid,
            sid,

            exit_code: 0,
            term_sig: 0,
            coredump: false,
            stop_sig: 0,
            stop_reported: false,
            continued: false,
            /* 定时器不跨 fork 继承（POSIX）：子进程从"未启动"开始。
             * exec 则保留，所以这里只在两个构造点初始化。 */
            itimer_next_us: 0,
            itimer_interval_us: 0,
        }
    }

    pub fn root_ppn(&self) -> PhysPageNum {
        self.user_space.root_ppn()
    }

    pub fn new_fd_table() -> Vec<Option<FileRef>> {
        let mut fd_table = Vec::new();

        /*
         * fd 0: stdin
         * fd 1: stdout
         * fd 2: stderr，暂时也接 stdout
         */
        fd_table.push(Some(crate::fs::stdin()));
        fd_table.push(Some(crate::fs::stdout()));
        fd_table.push(Some(crate::fs::stdout()));

        fd_table
    }

    pub fn close_non_standard_fds_on_exec(&mut self) -> Vec<FileRef> {
        let mut closed = Vec::new();
        for fd in 0..self.fd_table.len() {
            let cloexec = self.fd_cloexec.get(fd).copied().unwrap_or(false);

            /* FD_CLOEXEC 对任何 fd 生效（含 0/1/2）；没有标 cloexec 的
             * 仍按老规则：标准 fd 保留、其余关闭。 */
            if fd < 3 && !cloexec {
                continue;
            }

            if let Some(file) = self.fd_table[fd].take() {
                self.free_fds.push(fd);
                closed.push(file);
            }
        }

        /* 关掉的 fd 会被复用，标志必须清 —— 否则下一轮 exec 会把
         * "复用了同一个编号的新 fd"一起关掉。 */
        for v in self.fd_cloexec.iter_mut() {
            *v = false;
        }

        closed   // 把关掉的 file 返回出去
    }
}

impl ProcessControlBlock {
    /// 查询环境变量，返回值的不可变引用。
    pub fn env_get(&self, key: &str) -> Option<&String> {
        self.env.iter().find(|(k, _)| k == key).map(|(_, v)| v)
    }

    /// 设置环境变量。overwrite=false 且 key 已存在时不做改动。
    /// key 为空返回 false（非法）。
    pub fn env_set(&mut self, key: String, val: String, overwrite: bool) -> bool {
        if key.is_empty() {
            return false;
        }
        for slot in self.env.iter_mut() {
            if slot.0 == key {
                if overwrite {
                    slot.1 = val;
                }
                return true;
            }
        }
        self.env.push((key, val));
        true
    }

    /// 删除环境变量（不存在也安全）。
    pub fn env_unset(&mut self, key: &str) {
        if let Some(pos) = self.env.iter().position(|(k, _)| k == key) {
            self.env.remove(pos);
        }
    }

    /// 清空全部环境变量。
    pub fn env_clear(&mut self) {
        self.env.clear();
    }

    /// 复制整张环境表（exec 构造 envp 与用户态枚举用）。
    pub fn env_pairs(&self) -> Vec<(String, String)> {
        self.env.clone()
    }
}


#[derive(Clone, Copy, Debug)]
pub struct MmapArea {
    pub start: usize,
    pub end: usize,
    pub prot: usize,
}

#[derive(Clone, Copy, Debug)]
pub struct MmapFreeRange {
    pub start: usize,
    pub end: usize,
}

pub const USER_MMAP_BASE: usize = 0x4000_0000;
pub const USER_MMAP_TOP: usize = 0x7000_0000;

pub const PROT_READ: usize = 1;
pub const PROT_WRITE: usize = 2;
pub const PROT_EXEC: usize = 4;

impl ProcessControlBlock {
    pub fn alloc_mmap_range(&mut self, len: usize) -> Option<(usize, usize)> {
        let len = crate::mm::align_up(len, crate::mm::config::PAGE_SIZE);

        if len == 0 {
            return None;
        }

        // 优先复用 free range。先只做 first-fit。
        for i in 0..self.mmap_free_ranges.len() {
            let range = self.mmap_free_ranges[i];

            let start = crate::mm::align_up(
                range.start,
                crate::mm::config::PAGE_SIZE,
            );

            let end = start.checked_add(len)?;

            if end > range.end {
                continue;
            }

            //从这个 free range 里切出 [start, end)。
            if start == range.start && end == range.end {
                self.mmap_free_ranges.remove(i);
            } else if start == range.start {
                self.mmap_free_ranges[i].start = end;
            } else if end == range.end {
                self.mmap_free_ranges[i].end = start;
            } else {
                //中间切一段，拆成左右两个 free range。
                self.mmap_free_ranges[i].end = start;
                self.mmap_free_ranges.push(MmapFreeRange {
                    start: end,
                    end: range.end,
                });
            }

            return Some((start, end));
        }

        //free list 找不到，再从 mmap_next 扩张。
        let start = crate::mm::align_up(
            self.mmap_next,
            crate::mm::config::PAGE_SIZE,
        );

        let end = start.checked_add(len)?;

        if end > USER_MMAP_TOP {
            return None;
        }

        self.mmap_next = end;

        Some((start, end))
    }

    pub fn dealloc_mmap_range(&mut self, start: usize, end: usize) {
        if start >= end {
            return;
        }

        self.mmap_free_ranges.push(MmapFreeRange {
            start,
            end,
        });

        self.merge_mmap_free_ranges();
    }

    fn merge_mmap_free_ranges(&mut self) {
        //free range 数量一般不大，直接 O(n2) 合并，简单可靠。
        let mut changed = true;

        while changed {
            changed = false;

            'outer: for i in 0..self.mmap_free_ranges.len() {
                for j in (i + 1)..self.mmap_free_ranges.len() {
                    let a = self.mmap_free_ranges[i];
                    let b = self.mmap_free_ranges[j];

                    
                    //重叠或相邻都合并。
                    if a.end >= b.start && b.end >= a.start {
                        let start = if a.start < b.start { a.start } else { b.start };
                        let end = if a.end > b.end { a.end } else { b.end };

                        self.mmap_free_ranges[i] = MmapFreeRange {
                            start,
                            end,
                        };

                        self.mmap_free_ranges.remove(j);
                        changed = true;
                        break 'outer;
                    }
                }
            }
        }
    }
}