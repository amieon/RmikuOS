// arch/loongarch64/boot.S

.section .text.init
.globl _entry
_entry:
    # DMW 配置 
    li.d        $t0, 0x8000000000000001
    csrwr       $t0, LOONGARCH_CSR_DMWIN0
    li.d        $t0, (0x9000000000000001 | 1 << 4)
    csrwr       $t0, LOONGARCH_CSR_DMWIN1
    // 关闭 DMW2 和 DMW3
    li.d        $t0, 0x0000000000000000
    csrwr       $t0, LOONGARCH_CSR_DMWIN2
    csrwr       $t0, LOONGARCH_CSR_DMWIN3
    csrwr       $t0, LOONGARCH_CSR_TLBRENTRY

    # CPU 状态 
    li.w        $t0, 0xb0
    csrwr       $t0, LOONGARCH_CSR_CRMD
    li.w        $t0, 0x00
    csrwr       $t0, LOONGARCH_CSR_PRMD
    li.w        $t0, 0x00
    csrwr       $t0, LOONGARCH_CSR_EUEN

    # 栈设置（支持 16 核，每核 4KB）
    la.global   $sp, boot_stack
    li.d        $a0, 4096
    csrrd       $a1, LOONGARCH_CSR_CPUID
    addi.d      $tp, $a1, 0
    addi.d      $a1, $a1, 1
    mul.d       $a0, $a0, $a1
    add.d       $sp, $sp, $a0

    # 读取 CPU 配置，跳转到 Rust 
    li.d        $t0, 0x1
    cpucfg      $t1, $t0
    bl          rust_main      

spin:
    b           spin

# 栈空间定义 
.section .bss.stack
.align 12                      # 4KB 对齐
.globl boot_stack
boot_stack:
    .space 4096 * 4 * 4        # 64KB = 16核 × 4KB
.globl boot_stack_top
boot_stack_top: