use crate::mm::PhysPageNum;

pub fn activate_kernel_page_table(_root_ppn: PhysPageNum) {
    log::warn!("[mm] LoongArch paging activation is not implemented yet");
}