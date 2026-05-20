// build.rs
fn main() {
    let target = std::env::var("TARGET").unwrap();

    if target.contains("riscv64") {
        cc::Build::new()
            .file("kernel/src/arch/riscv64/boot.S")
            .flag("-march=rv64gc")
            .flag("-mabi=lp64d")
            .compile("boot_riscv64");
        println!("cargo:rustc-link-arg=-Tkernel/src/arch/riscv64/linker.ld");
    } else if target.contains("loongarch64") {
        
    }
}