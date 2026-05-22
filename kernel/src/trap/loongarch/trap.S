    .altmacro
    .macro SAVE_GP n
        st.d    $r\n, $sp, \n*8
    .endm
    .macro LOAD_GP n
        ld.d    $r\n, $sp, \n*8
    .endm

    .section .text.vectors
    .globl  __alltraps
    .globl  __restore
    .align  2

__alltraps:
    # 保存用户栈，切到内核栈
    la.abs      $t0, __user_sp
    st.d        $sp, $t0, 0         # __user_sp = 用户态 sp
    
    # 从 per-cpu 结构取内核栈顶
    csrrd       $t1, 0x20           # CPUID
    la.abs      $t0, __kernel_stacks
    li.d        $t2, 4096           # 每个核 4KB 栈
    mul.d       $t1, $t1, $t2
    add.d       $t0, $t0, $t1       # &kernel_stack[cpuid * 4096]
    addi.d      $sp, $t0, 4096      # sp = 栈顶（向下增长）

    # 分配 TrapFrame
    addi.d      $sp, $sp, -34*8

    # 保存通用寄存器
    st.d        $ra, $sp, 1*8       # x1
    # x2(sp) 保存在 2*8，后面写
    st.d        $tp, $sp, 3*8       # x4
    
    .set        n, 5
    .rept       27                  # x5~x31 ($r5~$r31)
        SAVE_GP     %n
        .set        n, n+1
    .endr

    # 保存 CSR
    csrrd       $t0, 0x6            # ERA
    st.d        $t0, $sp, 32*8
    csrrd       $t0, 0x1            # PRMD
    st.d        $t0, $sp, 33*8

    # 保存原始用户栈
    la.abs      $t0, __user_sp
    ld.d        $t0, $t0, 0
    st.d        $t0, $sp, 2*8       # sp slot

    # 调用 Rust handler
    move        $a0, $sp            # &mut TrapFrame
    bl          trap_handler

# ========== 返回入口 ==========
__restore:
    move        $sp, $a0            # a0 = &TrapFrame

    # 恢复 CSR
    ld.d        $t0, $sp, 32*8
    csrwr       $t0, 0x6            # ERA
    ld.d        $t0, $sp, 33*8
    csrwr       $t0, 0x1            # PRMD

    # 恢复用户栈
    ld.d        $t0, $sp, 2*8       # 用户态 sp
    la.abs      $t1, __user_sp
    st.d        $t0, $t1, 0         # 写回 __user_sp

    # 恢复通用寄存器
    ld.d        $ra, $sp, 1*8
    ld.d        $tp, $sp, 3*8
    
    .set        n, 5
    .rept       27
        LOAD_GP     %n
        .set        n, n+1
    .endr

    # 释放 TrapFrame，切回用户栈
    ld.d        $sp, $sp, 2*8       # sp = 用户态 sp（直接从 TrapFrame 读）
    ertn

    .section    .data
    .align      3
__user_sp:
    .dword      0

    .globl      __kernel_stacks
    .align      12
__kernel_stacks:
    .space      4096 * 4            # 4核 * 4KB