//! RISC-V trap context.

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct TrapContext {
    /// General registers x0..x31.
    pub x: [usize; 32],
    /// CSR sstatus.
    pub sstatus: usize,
    /// CSR sepc.
    pub sepc: usize,
    /// CSR stval.
    pub stval: usize,
    /// CSR scause.
    pub scause: usize,

    pub f: [u64; 32],      
    pub fcsr: usize,          
}

impl TrapContext {
    pub const fn zero() -> Self {
        Self {
            x: [0; 32],
            sstatus: 0,
            sepc: 0,
            stval: 0,
            scause: 0,
            f: [0; 32],
            fcsr: 0,
        }
    }

    pub fn set_sp(&mut self, sp: usize) {
        self.x[REG_SP] = sp;
    }

    pub fn user_sp(&self) -> usize {
        self.x[REG_SP]
    }

    pub fn user_pc(&self) -> usize {
        self.sepc
    }

    pub fn set_user_pc(&mut self, pc: usize) {
        self.sepc = pc;
    }

    pub fn app_init_context(entry: usize, sp: usize) -> Self {
        const SSTATUS_SPIE: usize = 1 << 5;
        const SSTATUS_FS_INITIAL: usize = 1 << 13;  

        let mut cx = Self::zero();
        cx.sstatus = SSTATUS_SPIE | SSTATUS_FS_INITIAL;  
        cx.sepc = entry;
        cx.set_sp(sp);
        cx
    }

    pub fn syscall_id(&self) -> usize {
        self.x[REG_A7]
    }

    pub fn syscall_args(&self) -> [usize; 3] {
        [
            self.x[REG_A0],
            self.x[REG_A1],
            self.x[REG_A2],
        ]
    }

    pub fn set_syscall_ret(&mut self, ret: usize) {
        self.x[REG_A0] = ret;
    }
    pub fn syscall_ret(&self) -> usize {
        self.x[REG_A0]
    }

    pub fn advance_pc(&mut self) {
        self.sepc += INSTRUCTION_SIZE;
    }

    pub fn is_interrupt(&self) -> bool {
        self.scause & SCAUSE_INTERRUPT_BIT != 0
    }

    pub fn cause_code(&self) -> usize {
        self.scause & !SCAUSE_INTERRUPT_BIT
    }
}

const REG_SP: usize = 2;
const REG_A0: usize = 10;
const REG_A1: usize = 11;
const REG_A2: usize = 12;
const REG_A7: usize = 17;

const INSTRUCTION_SIZE: usize = 4;

const SSTATUS_SPIE: usize = 1 << 5;
const SSTATUS_SPP: usize = 1 << 8;

const SCAUSE_INTERRUPT_BIT: usize = 1usize << (usize::BITS as usize - 1);


impl TrapContext {
    pub fn is_from_user(&self) -> bool {
        const SSTATUS_SPP: usize = 1 << 8;
        self.sstatus & SSTATUS_SPP == 0
    }
}

impl TrapContext {
    pub fn set_app_args(&mut self, argc: usize, argv: usize) {
        self.x[REG_A0] = argc; // a0
        self.x[REG_A1] = argv; // a1
    }
}

impl TrapContext {
    pub fn set_thread_args(&mut self, func: usize, arg: usize) {
        self.set_app_args(func, arg);
    }
}