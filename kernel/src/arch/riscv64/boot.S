.section .text.init
.globl _start
_start:
    # 多核栈分配
    la   sp, boot_stack
    li   t0, 4096 * 4           # 每核 16KB 栈（比 4KB 更安全）
    
    mv   t1, a0                 # t1 = hartid（SBI 传入）
    addi t1, t1, 1             # t1 = hartid + 1
    mul  t0, t0, t1
    add  sp, sp, t0            # sp = boot_stack + 16KB*(hartid+1)
    
    # 保存 hartid 到 tp 寄存器
    mv   tp, a0                # tp = hartid，后续 cpuid() 直接从 tp 读
    

    # 把 hartid 作为第一个参数传给 rust_main
    call rust_main              # rust_main(hartid: usize)
    
spin:
    j    spin
.section .bss.stack
.align 12                      # 4KB 对齐
.globl boot_stack
boot_stack:
    .space 4096 * 16 * 4       # 256KB
.globl boot_stack_top
boot_stack_top: