    .section .text
    .global syscall3
    .type syscall3, @function

syscall3:
    /*
     * C ABI:
     *   a0/r4 = id
     *   a1/r5 = arg0
     *   a2/r6 = arg1
     *   a3/r7 = arg2
     *
     * Kernel syscall ABI:
     *   r11 = id
     *   r4  = arg0
     *   r5  = arg1
     *   r6  = arg2
     */
    or      $r11, $r4, $r0
    or      $r4,  $r5, $r0
    or      $r5,  $r6, $r0
    or      $r6,  $r7, $r0
    syscall 0
    jirl    $zero, $r1, 0



    .section .text
    .global syscall6
    .type syscall6, @function

syscall6:
    /*
     * C ABI:
     *   syscall6(id, arg0, arg1, arg2, arg3, arg4, arg5)
     *
     * On entry:
     *   a0/r4  = id
     *   a1/r5  = arg0
     *   a2/r6  = arg1
     *   a3/r7  = arg2
     *   a4/r8  = arg3
     *   a5/r9  = arg4
     *   a6/r10 = arg5
     *
     * Kernel syscall ABI:
     *   r11 = id
     *   r4  = arg0
     *   r5  = arg1
     *   r6  = arg2
     *   r7  = arg3
     *   r8  = arg4
     *   r9  = arg5
     */
    or      $r11, $r4,  $r0
    or      $r4,  $r5,  $r0
    or      $r5,  $r6,  $r0
    or      $r6,  $r7,  $r0
    or      $r7,  $r8,  $r0
    or      $r8,  $r9,  $r0
    or      $r9,  $r10, $r0
    syscall 0
    jirl    $zero, $r1, 0


/* ------------------------------------------------------------------
 * setjmp / longjmp —— 纯汇编实现。
 *
 * 为什么不用 C 的 static __attribute__((naked)):
 * loongarch64-unknown-linux-gnu-gcc(>= 15)对 naked 支持有 bug,
 * 会给函数生成 prologue(sp-=32; fp 指向自己的帧)却绕过 epilogue
 * 直接 ret, 导致 setjmp 返回后调用方 $fp 被污染、保存进 env 的
 * sp/fp 也是 setjmp 自己的错误值(表现为 setjmp 返回后调用方
 * 用 $fp 读局部变量全部读到垃圾/0, 例如 Lua 启动即跳 0)。
 *
 * 布局与 glibc sysdeps/loongarch/__sigsetjmp.S 一致(21 个 8 字节):
 *   0=ra  8=sp  16=r21(ABI 保留)  24=fp
 *   32..96=s0..s8(9 个 GPR callee-saved)
 *   104..160=f24..f31(8 个浮点 callee-saved, LP64D)
 * 对应 include/setjmp.h 中 loongarch64 分支的 jmp_buf。
 * 本文件随 _syscall.o 链接进所有 C 项目, 故此处提供全局符号即可。
 * ------------------------------------------------------------------ */
    .section .text
    .global setjmp
    .type setjmp, @function

setjmp:
    /* env 在 $a0; 纯汇编无 prologue, $sp/$fp 就是调用方的值 */
    st.d    $r1,  $a0, 0
    st.d    $r3,  $a0, 8
    st.d    $r21, $a0, 16
    st.d    $r22, $a0, 24
    st.d    $r23, $a0, 32
    st.d    $r24, $a0, 40
    st.d    $r25, $a0, 48
    st.d    $r26, $a0, 56
    st.d    $r27, $a0, 64
    st.d    $r28, $a0, 72
    st.d    $r29, $a0, 80
    st.d    $r30, $a0, 88
    st.d    $r31, $a0, 96
    fst.d   $f24, $a0, 104
    fst.d   $f25, $a0, 112
    fst.d   $f26, $a0, 120
    fst.d   $f27, $a0, 128
    fst.d   $f28, $a0, 136
    fst.d   $f29, $a0, 144
    fst.d   $f30, $a0, 152
    fst.d   $f31, $a0, 160
    move    $a0, $zero            /* setjmp 首次返回 0 */
    jirl    $zero, $r1, 0
    .size setjmp, .-setjmp

    .global longjmp
    .type longjmp, @function

longjmp:
    /* env 在 $a0, val 在 $a1; 先恢复 callee-saved(用 $a0 作基址) */
    ld.d    $r23, $a0, 32
    ld.d    $r24, $a0, 40
    ld.d    $r25, $a0, 48
    ld.d    $r26, $a0, 56
    ld.d    $r27, $a0, 64
    ld.d    $r28, $a0, 72
    ld.d    $r29, $a0, 80
    ld.d    $r30, $a0, 88
    ld.d    $r31, $a0, 96
    fld.d   $f24, $a0, 104
    fld.d   $f25, $a0, 112
    fld.d   $f26, $a0, 120
    fld.d   $f27, $a0, 128
    fld.d   $f28, $a0, 136
    fld.d   $f29, $a0, 144
    fld.d   $f30, $a0, 152
    fld.d   $f31, $a0, 160
    ld.d    $r22, $a0, 24         /* fp */
    ld.d    $r3,  $a0, 8          /* sp(恢复后不再访旧栈) */
    ld.d    $r1,  $a0, 0          /* ra: 仍用 $a0(env)作基址, 与 sp 无关 */
    ld.d    $r21, $a0, 16
    sltui   $a0, $a1, 1           /* a0 = (val == 0) ? 1 : 0 */
    add.d   $a0, $a0, $a1         /* 合成: val==0 -> 1, 否则 -> val */
    jirl    $zero, $r1, 0
    .size longjmp, .-longjmp