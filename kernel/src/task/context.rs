#[repr(C)]
#[derive(Clone, Copy)]
pub struct TaskContext {
    pub ra: usize,
    pub sp: usize,

    #[cfg(target_arch = "riscv64")]
    pub s: [usize; 12],

    #[cfg(target_arch = "loongarch64")]
    pub s: [usize; 10],
}

impl TaskContext {
    pub const fn zero() -> Self {
        Self {
            ra: 0,
            sp: 0,

            #[cfg(target_arch = "riscv64")]
            s: [0; 12],

            #[cfg(target_arch = "loongarch64")]
            s: [0; 10],
        }
    }

    pub fn goto_task_entry(sp: usize) -> Self {
        extern "C" {
            fn __task_entry() -> !;
        }

        Self {
            ra: __task_entry as usize,
            sp,

            #[cfg(target_arch = "riscv64")]
            s: [0; 12],

            #[cfg(target_arch = "loongarch64")]
            s: [0; 10],
        }
    }
}
