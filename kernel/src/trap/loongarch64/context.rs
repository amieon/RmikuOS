//! LoongArch64 trap context.

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct TrapContext {
    /// General registers r0..r31.
    pub r: [usize; 32],
    /// CSR.PRMD: previous PLV/interrupt state.
    pub prmd: usize,
    /// CSR.ERA: exception return address.
    pub era: usize,
    /// CSR.BADV: bad virtual address.
    pub badv: usize,
    /// CSR.ESTAT: exception status.
    pub estat: usize,

    pub f: [u64; 32],      
    pub fcsr: usize, 
    pub _pad: usize, // 552 -> 560
}

impl TrapContext {
    pub const fn zero() -> Self {
        Self {
            r: [0; 32],
            prmd: 0,
            era: 0,
            badv: 0,
            estat: 0,
            f :[0; 32],
            fcsr: 0,
            _pad: 0,
        }
    }



    pub fn set_sp(&mut self, sp: usize) {
        self.r[REG_SP] = sp;
    }

    pub fn user_sp(&self) -> usize {
        self.r[REG_SP]
    }

    pub fn user_pc(&self) -> usize {
        self.era
    }

    pub fn set_user_pc(&mut self, pc: usize) {
        self.era = pc;
    }

    pub fn app_init_context(entry: usize, sp: usize) -> Self {
        const PRMD_PPLV_USER: usize = 3;
        const PRMD_PIE: usize = 1 << 2;

        let mut cx = Self::zero();
        cx.prmd = PRMD_PPLV_USER | PRMD_PIE;
        cx.era = entry;
        cx.set_sp(sp);
        cx
    }

    pub fn syscall_id(&self) -> usize {
        self.r[REG_A7]
    }

    pub fn syscall_args(&self) -> [usize; 3] {
        [
            self.r[REG_A0],
            self.r[REG_A1],
            self.r[REG_A2],
        ]
    }

    pub fn set_syscall_ret(&mut self, ret: usize) {
        self.r[REG_A0] = ret;
    }
    pub fn syscall_ret(&self) -> usize {
        self.r[REG_A0]
    }
    pub fn advance_pc(&mut self) {
        self.era += INSTRUCTION_SIZE;
    }

    pub fn ecode(&self) -> usize {
        (self.estat >> 16) & 0x3f
    }

    pub fn esubcode(&self) -> usize {
        (self.estat >> 22) & 0x1ff
    }

    pub fn interrupt_pending_bits(&self) -> usize {
        self.estat & 0x1fff
    }
}

const REG_SP: usize = 3;
const REG_A0: usize = 4;
const REG_A1: usize = 5;
const REG_A2: usize = 6;
const REG_A7: usize = 11;

const INSTRUCTION_SIZE: usize = 4;

const PRMD_PPLV_USER: usize = 3;
const PRMD_PIE: usize = 1 << 2;


impl TrapContext {
    pub fn is_from_user(&self) -> bool {
        const PRMD_PPLV_MASK: usize = 0x3;
        const PLV_USER: usize = 0x3;

        self.prmd & PRMD_PPLV_MASK == PLV_USER
    }
}

impl TrapContext {
    pub fn set_app_args(&mut self, argc: usize, argv: usize) {
        self.r[REG_A0] = argc; // a0
        self.r[REG_A1] = argv; // a1
    }
}

impl TrapContext {
    pub fn set_thread_args(&mut self, func: usize, arg: usize) {
        self.set_app_args(func, arg);
    }
}