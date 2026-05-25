// kernel/src/arch/loongarch64/trap/context.rs

#[repr(C)]
pub struct TrapContext {
    pub r: [usize; 32],
    pub prmd: usize,
    pub era: usize,
    pub badv: usize,
    pub estat: usize,
}

impl TrapContext {
    pub fn set_sp(&mut self, sp: usize) {
        self.r[3] = sp;
    }

    pub fn app_init_context(entry: usize, sp: usize) -> Self {
        let mut cx = Self {
            r: [0; 32],
            prmd: 3 | (1 << 2), // PPLV=User, PIE=1
            era: entry,
            badv: 0,
            estat: 0,
        };
        cx.set_sp(sp);
        cx
    }

    pub fn ecode(&self) -> usize {
        (self.estat >> 16) & 0x3f
    }

    pub fn esubcode(&self) -> usize {
        (self.estat >> 22) & 0x1ff
    }
}