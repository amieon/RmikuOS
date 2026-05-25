//! RISC-V trap context.
//!
//! This is intentionally architecture-local.  It mirrors what trap.S saves
//! on the current kernel stack.  Later, when user tasks are added, this can
//! be embedded into a TaskControlBlock instead of using a global context array.

#[repr(C)]
#[derive(Debug)]
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
}

impl TrapContext {
    pub const fn zero() -> Self {
        Self {
            x: [0; 32],
            sstatus: 0,
            sepc: 0,
            stval: 0,
            scause: 0,
        }
    }

    /// RISC-V stack pointer is x2/sp.
    pub fn set_sp(&mut self, sp: usize) {
        self.x[2] = sp;
    }

    /// Minimal user-context initializer for the future.
    ///
    /// SPP=0 means return to user mode on sret. SPIE=1 means interrupts will
    /// be enabled after sret.  This function is not used by the current kernel
    /// loop yet, but keeping it here makes the later user-mode step clear.
    pub fn app_init_context(entry: usize, sp: usize) -> Self {
        const SSTATUS_SPIE: usize = 1 << 5;
        const SSTATUS_SPP: usize = 1 << 8;

        let mut cx = Self::zero();
        cx.sstatus = SSTATUS_SPIE & !SSTATUS_SPP;
        cx.sepc = entry;
        cx.set_sp(sp);
        cx
    }

    pub fn is_interrupt(&self) -> bool {
        const INTERRUPT_BIT: usize = 1usize << (usize::BITS as usize - 1);
        self.scause & INTERRUPT_BIT != 0
    }

    pub fn cause_code(&self) -> usize {
        const INTERRUPT_BIT: usize = 1usize << (usize::BITS as usize - 1);
        self.scause & !INTERRUPT_BIT
    }
}
