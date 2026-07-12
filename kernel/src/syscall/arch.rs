
pub fn shutdown() -> isize {
    crate::arch::shutdown();
    -1
}