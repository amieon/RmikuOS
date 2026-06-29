RV_TARGET := riscv64gc-unknown-none-elf
LA_TARGET := loongarch64-unknown-none

RV_ELF := target/$(RV_TARGET)/release/RmikuOS
LA_ELF := target/$(LA_TARGET)/release/RmikuOS

.PHONY: all restore-cargo kernel-rv kernel-la clean

all: restore-cargo kernel-rv kernel-la


restore-cargo:
	@if [ -d cargo-config ] && [ ! -d .cargo ]; then \
		echo "=== 还原 .cargo 目录 ==="; \
		cp -r cargo-config .cargo; \
	fi


kernel-rv: restore-cargo
	@echo "=== 编译 RISC-V 内核 (oscomp) ==="
	CARGO_BUILD_TARGET=$(RV_TARGET) cargo build --release --features oscomp --target $(RV_TARGET)
	cp $(RV_ELF) kernel-rv
	@echo "=== 生成 kernel-rv ==="

kernel-la: restore-cargo
	@echo "=== 编译 LoongArch 内核 (oscomp) ==="
	CARGO_BUILD_TARGET=$(LA_TARGET) cargo build --release --features oscomp --target $(LA_TARGET)
	cp $(LA_ELF) kernel-la
	@echo "=== 生成 kernel-la ==="

clean:
	cargo clean
	rm -f kernel-rv kernel-la