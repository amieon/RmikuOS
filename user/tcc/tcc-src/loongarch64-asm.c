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

ST_FUNC void g(int c);
ST_FUNC void gen_le16(int c);
ST_FUNC void gen_le32(int c);
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

/*************************************************************/
#else
/*************************************************************/
#define USING_GLOBALS
#include "tcc.h"

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
