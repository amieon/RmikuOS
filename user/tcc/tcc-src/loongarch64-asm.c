/*************************************************************/
/*
 *  LOONGARCH64 assembler for TCC  (phase-1 stub)
 *
 *  Phase 1 of the LoongArch64 backend ships codegen (loongarch64-gen.c)
 *  and linker (loongarch64-link.c) only; the assembler is a stub that
 *  satisfies the linker's symbol references.  Every entry point errors
 *  out with "asm not supported".
 *
 *  To implement real assembly support later, replace the bodies below
 *  (model them on riscv64-asm.c) and add the token tables.
 *
 */

#ifdef TARGET_DEFS_ONLY

/* NOTE: we deliberately do NOT define CONFIG_TCC_ASM here -- that would
   pull the whole tccasm.c GAS parser into the build.  With it undefined,
   tccasm.c compiles its "asm not supported" stub instead, and the
   functions below are never actually called; they exist only so a link
   succeeds no matter which translation unit was cached. */
#define NB_ASM_REGS 32

/* 仅声明无类型依赖的函数(ASMOperand/SValue/ExprValue/CString 在
   tcc.h 之后才定义, TARGET_DEFS_ONLY 模式下不可见) */
ST_FUNC void g(int c);
ST_FUNC void gen_le16(int c);
ST_FUNC void gen_le32(int c);

/*************************************************************/
#else
/*************************************************************/
#define USING_GLOBALS
#include "tcc.h"

/* ASMOperand/ExprValue 定义在 tcc.h �� `#ifdef CONFIG_TCC_ASM` 内, 而
   loongarch64 阶段 1 故意不定义 CONFIG_TCC_ASM(asm 禁用, 避免拉起整个
   tccasm.c GAS 解析器)。这里自行补充这两个类型的定义, 仅供本 stub 的
   函数签名使用; 将来启用 CONFIG_TCC_ASM 时本块自动失效, 使用 tcc.h 的
   正式定义(字段保持一致)。 */
#ifndef CONFIG_TCC_ASM
typedef struct ExprValue {
    uint64_t v;
    Sym *sym;
    int pcrel;
} ExprValue;

#define MAX_ASM_OPERANDS 30
typedef struct ASMOperand {
    int id; /* GCC 3 optional identifier (0 if number only supported) */
    char constraint[16];
    char asm_str[16]; /* computed asm string for operand */
    SValue *vt; /* C value of the expression */
    int ref_index; /* if >= 0, gives reference to a output constraint */
    int input_index; /* if >= 0, gives reference to an input constraint */
    int priority; /* priority, used to assign registers */
    int reg; /* if >= 0, register number used for this operand */
    int is_llong; /* true if double register value */
    int is_memory; /* true if memory operand */
    int is_rw;     /* for '+' modifier */
    int is_label;  /* for asm goto */
} ASMOperand;
#endif

/* 其余 asm 符号的声明(此时 SValue/ASMOperand/ExprValue/CString 已定义) */
ST_FUNC void asm_clobber(uint8_t *clobber_regs, const char *str);
ST_FUNC void asm_compute_constraints(ASMOperand *operands, int nb_operands,
                                     int nb_outputs, const uint8_t *clobber_regs,
                                     int *pout_reg);
ST_FUNC void asm_gen_code(ASMOperand *operands, int nb_operands,
                          int nb_outputs, int is_output,
                          uint8_t *clobber_regs, int out_reg);
ST_FUNC void asm_opcode(TCCState *s1, int token);
ST_FUNC int asm_parse_regvar(int t);
ST_FUNC void gen_expr32(ExprValue *pe);
ST_FUNC void subst_asm_operand(CString *add_str, SValue *sv, int modifier);

/* ---- byte emission (used by tccasm.c even when asm is disabled) ---- */
ST_FUNC void g(int c)
{
    if (nocode_wanted)
        return;
    section_realloc(cur_text_section, ind + 1);
    cur_text_section->data[ind++] = c;
}

ST_FUNC void gen_le16(int c)
{
    g(c);
    g(c >> 8);
}

ST_FUNC void gen_le32(int c)
{
    gen_le16(c);
    gen_le16(c >> 16);
}

ST_FUNC void gen_expr32(ExprValue *pe)
{
    gen_le32(pe->v);
}

/* ---- assembler entry points (phase-1 stubs) ---- */
ST_FUNC void asm_opcode(TCCState *s1, int token)
{
    tcc_error("asm not supported on LoongArch64 yet");
}

ST_FUNC int asm_parse_regvar(int t)
{
    tcc_error("asm not supported on LoongArch64 yet");
    return -1;
}

ST_FUNC void asm_clobber(uint8_t *clobber_regs, const char *str)
{
    tcc_error("asm not supported on LoongArch64 yet");
}

ST_FUNC void asm_compute_constraints(ASMOperand *operands, int nb_operands,
                                     int nb_outputs, const uint8_t *clobber_regs,
                                     int *pout_reg)
{
    tcc_error("asm not supported on LoongArch64 yet");
}

ST_FUNC void asm_gen_code(ASMOperand *operands, int nb_operands,
                          int nb_outputs, int is_output,
                          uint8_t *clobber_regs, int out_reg)
{
    tcc_error("asm not supported on LoongArch64 yet");
}

ST_FUNC void subst_asm_operand(CString *add_str, SValue *sv, int modifier)
{
    tcc_error("asm not supported on LoongArch64 yet");
}

/*************************************************************/
#endif /* TARGET_DEFS_ONLY */
