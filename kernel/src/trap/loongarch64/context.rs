//! LoongArch64 trap context.
//!
//! This mirrors what trap.S saves on the current kernel stack.  It is similar
//! in spirit to rCore's TrapContext, but LoongArch uses PRMD/ERA instead of
//! sstatus/sepc.

#[repr(C)]
#[derive(Debug)]
pub struct TrapContext {
    /// General registers r0..r31.
    pub r: [usize; 32],
    /// CSR.PRMD: previous PLV/interrupt state.
    pub prmd: usize,
    /// CSR.ERA: exception return address.
    pub era: usize,
    /// CSR.BADV: bad virtual address.
    pub badv: usize,
    /// CSR.ESTAT: exception status, including Ecode/EsubCode and interrupt bits.
    pub estat: usize,
}

impl TrapContext {
    pub const fn zero() -> Self {
        Self {
            r: [0; 32],
            prmd: 0,
            era: 0,
            badv: 0,
            estat: 0,
        }
    }

    /// LoongArch stack pointer is r3/sp.
    pub fn set_sp(&mut self, sp: usize) {
        self.r[3] = sp;
    }

    /// Minimal user-context initializer for the future.
    ///
    /// PRMD.PPLV=3 returns to user PLV3 on ertn.  PRMD.PIE=1 means interrupt
    /// enable is restored to enabled after ertn.
    pub fn app_init_context(entry: usize, sp: usize) -> Self {
        const PRMD_PPLV_USER: usize = 3;
        const PRMD_PIE: usize = 1 << 2;

        let mut cx = Self::zero();
        cx.prmd = PRMD_PPLV_USER | PRMD_PIE;
        cx.era = entry;
        cx.set_sp(sp);
        cx
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
