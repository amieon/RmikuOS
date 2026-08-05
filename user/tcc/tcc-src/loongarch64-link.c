/* LoongArch64 ELF linker / relocations for TCC */
#ifdef TARGET_DEFS_ONLY

#define EM_TCC_TARGET 258   /* EM_LOONGARCH */

/* LoongArch ELF relocations (LoongArch ELF ABI v2.01).
   R_LARCH_* are NOT in elf.h (unlike R_RISCV_*), and tcc.h includes
   the backends with TARGET_DEFS_ONLY defined, so this branch is the
   only part of link.c seen by tccgen.c/tccelf.c/etc.  The full table
   must therefore be self-contained HERE (the #else branch below
   repeats it under #ifndef R_LARCH_NONE, which is then skipped). */
#ifndef R_LARCH_NONE
#define R_LARCH_NONE          0
#define R_LARCH_32            1
#define R_LARCH_64            2
#define R_LARCH_RELATIVE      3
#define R_LARCH_COPY          4
#define R_LARCH_JUMP_SLOT     5
#define R_LARCH_TLS_DTPMOD32  6
#define R_LARCH_TLS_DTPMOD64  7
#define R_LARCH_TLS_DTPREL32  8
#define R_LARCH_TLS_DTPREL64  9
#define R_LARCH_TLS_TPREL32  10
#define R_LARCH_TLS_TPREL64  11
#define R_LARCH_IRELATIVE    12
#define R_LARCH_MARK_LA      20
#define R_LARCH_MARK_PCREL   21
#define R_LARCH_ADD8         47
#define R_LARCH_ADD16        48
#define R_LARCH_ADD24        49
#define R_LARCH_ADD32        50
#define R_LARCH_ADD64        51
#define R_LARCH_SUB8         52
#define R_LARCH_SUB16        53
#define R_LARCH_SUB24        54
#define R_LARCH_SUB32        55
#define R_LARCH_SUB64        56
#define R_LARCH_GNU_VTINHERIT 57
#define R_LARCH_GNU_VTENTRY  58
#define R_LARCH_B16          64
#define R_LARCH_B21          65
#define R_LARCH_B26          66
#define R_LARCH_ABS_HI20     67
#define R_LARCH_ABS_LO12     68
#define R_LARCH_ABS64_LO20   69
#define R_LARCH_ABS64_HI12   70
#define R_LARCH_PCALA_HI20   71
#define R_LARCH_PCALA_LO12   72
#define R_LARCH_PCALA64_LO20 73
#define R_LARCH_PCALA64_HI12 74
#define R_LARCH_GOT_PC_HI20  75
#define R_LARCH_GOT_PC_LO12  76
#define R_LARCH_GOT64_PC_LO20 77
#define R_LARCH_GOT64_PC_HI12 78
#define R_LARCH_GOT_HI20     79
#define R_LARCH_GOT_LO12     80
#define R_LARCH_GOT64_LO20   81
#define R_LARCH_GOT64_HI12   82
#define R_LARCH_TLS_LE_HI20  83
#define R_LARCH_TLS_LE_LO12  84
#define R_LARCH_TLS_LE64_LO20 85
#define R_LARCH_TLS_LE64_HI12 86
#define R_LARCH_TLS_TPREL_HI20 87
#define R_LARCH_TLS_TPREL_LO12 88
#define R_LARCH_TLS_TPREL64_LO20 89
#define R_LARCH_TLS_TPREL64_HI12 90
#define R_LARCH_TLS_DTPREL_HI20 91
#define R_LARCH_TLS_DTPREL_LO12 92
#define R_LARCH_TLS_DTPREL64_LO20 93
#define R_LARCH_TLS_DTPREL64_HI12 94
#define R_LARCH_TLS_GD_PC_HI20 95
#define R_LARCH_TLS_GD_PC_LO12 96
#define R_LARCH_TLS_LD_PC_HI20 97
#define R_LARCH_TLS_LD_PC_LO12 98
#define R_LARCH_32_PCREL    99
/* relax 相关(psABI v2.x): 链接器松弛标记, 静态链接直接忽略 */
#define R_LARCH_DELETE      100
#define R_LARCH_CFA         101
#define R_LARCH_ALIGN       102
/* psABI v2.30: 可松弛的 PCALA 对(GCC14+ medium code model 会发,
   静态链接语义与 PCALA_HI20/LO12 完全相同) */
#define R_LARCH_PCALA64_LO12 105
#define R_LARCH_PCALA64_HI20 106
#endif

#define R_DATA_32  R_LARCH_32
#define R_DATA_PTR R_LARCH_64
#define R_JMP_SLOT R_LARCH_JUMP_SLOT
#define R_GLOB_DAT R_LARCH_64
#define R_COPY     R_LARCH_COPY
#define R_RELATIVE R_LARCH_RELATIVE

#define R_NUM      100

#define ELF_START_ADDR 0x00010000
#define ELF_PAGE_SIZE 0x1000

#define PCRELATIVE_DLLPLT 0
#define RELOCATE_DLLPLT 1

#else /* !TARGET_DEFS_ONLY */

//#define DEBUG_RELOC
#include "tcc.h"

/* LoongArch ELF relocations (LoongArch ELF ABI v2.01) */
#ifndef R_LARCH_NONE
#define R_LARCH_NONE          0
#define R_LARCH_32            1
#define R_LARCH_64            2
#define R_LARCH_RELATIVE      3
#define R_LARCH_COPY          4
#define R_LARCH_JUMP_SLOT     5
#define R_LARCH_TLS_DTPMOD32  6
#define R_LARCH_TLS_DTPMOD64  7
#define R_LARCH_TLS_DTPREL32  8
#define R_LARCH_TLS_DTPREL64  9
#define R_LARCH_TLS_TPREL32  10
#define R_LARCH_TLS_TPREL64  11
#define R_LARCH_IRELATIVE    12
#define R_LARCH_MARK_LA      20
#define R_LARCH_MARK_PCREL   21
#define R_LARCH_ADD8         47
#define R_LARCH_ADD16        48
#define R_LARCH_ADD24        49
#define R_LARCH_ADD32        50
#define R_LARCH_ADD64        51
#define R_LARCH_SUB8         52
#define R_LARCH_SUB16        53
#define R_LARCH_SUB24        54
#define R_LARCH_SUB32        55
#define R_LARCH_SUB64        56
#define R_LARCH_GNU_VTINHERIT 57
#define R_LARCH_GNU_VTENTRY  58
#define R_LARCH_B16          64
#define R_LARCH_B21          65
#define R_LARCH_B26          66
#define R_LARCH_ABS_HI20     67
#define R_LARCH_ABS_LO12     68
#define R_LARCH_ABS64_LO20   69
#define R_LARCH_ABS64_HI12   70
#define R_LARCH_PCALA_HI20   71
#define R_LARCH_PCALA_LO12   72
#define R_LARCH_PCALA64_LO20 73
#define R_LARCH_PCALA64_HI12 74
#define R_LARCH_GOT_PC_HI20  75
#define R_LARCH_GOT_PC_LO12  76
#define R_LARCH_GOT64_PC_LO20 77
#define R_LARCH_GOT64_PC_HI12 78
#define R_LARCH_GOT_HI20     79
#define R_LARCH_GOT_LO12     80
#define R_LARCH_GOT64_LO20   81
#define R_LARCH_GOT64_HI12   82
#define R_LARCH_TLS_LE_HI20  83
#define R_LARCH_TLS_LE_LO12  84
#define R_LARCH_TLS_LE64_LO20 85
#define R_LARCH_TLS_LE64_HI12 86
#define R_LARCH_TLS_TPREL_HI20 87
#define R_LARCH_TLS_TPREL_LO12 88
#define R_LARCH_TLS_TPREL64_LO20 89
#define R_LARCH_TLS_TPREL64_HI12 90
#define R_LARCH_TLS_DTPREL_HI20 91
#define R_LARCH_TLS_DTPREL_LO12 92
#define R_LARCH_TLS_DTPREL64_LO20 93
#define R_LARCH_TLS_DTPREL64_HI12 94
#define R_LARCH_TLS_GD_PC_HI20 95
#define R_LARCH_TLS_GD_PC_LO12 96
#define R_LARCH_TLS_LD_PC_HI20 97
#define R_LARCH_TLS_LD_PC_LO12 98
#define R_LARCH_32_PCREL    99
/* relax 相关(psABI v2.x): 链接器松弛标记, 静态链接直接忽略 */
#define R_LARCH_DELETE      100
#define R_LARCH_CFA         101
#define R_LARCH_ALIGN       102
/* psABI v2.30: 可松弛 PCALA 对, 语义同 PCALA_HI20/LO12 */
#define R_LARCH_PCALA64_LO12 105
#define R_LARCH_PCALA64_HI20 106
#endif

/* Returns 1 for a code relocation, 0 for a data relocation. For unknown
   relocations, returns -1. */
ST_FUNC int code_reloc (int reloc_type)
{
    switch (reloc_type) {

    case R_LARCH_B16:
    case R_LARCH_B21:
    case R_LARCH_B26:
    case R_LARCH_DELETE:   /* relax 标记, 在代码段, relocate() 忽略 */
    case R_LARCH_CFA:
    case R_LARCH_ALIGN:
        return 1;

    case R_LARCH_PCALA_HI20:
    case R_LARCH_PCALA_LO12:
    case R_LARCH_PCALA64_LO12:  /* 105: 同 PCALA_LO12 */
    case R_LARCH_PCALA64_HI20:  /* 106: 同 PCALA_HI20 */
    case R_LARCH_ABS_HI20:
    case R_LARCH_ABS_LO12:
    case R_LARCH_ABS64_LO20:
    case R_LARCH_ABS64_HI12:
    case R_LARCH_PCALA64_LO20:
    case R_LARCH_PCALA64_HI12:
    case R_LARCH_GOT_HI20:
    case R_LARCH_GOT_LO12:
    case R_LARCH_GOT_PC_HI20:
    case R_LARCH_GOT_PC_LO12:
    case R_LARCH_TLS_LE_HI20:
    case R_LARCH_TLS_LE_LO12:
    case R_LARCH_ADD8:
    case R_LARCH_ADD16:
    case R_LARCH_ADD24:
    case R_LARCH_ADD32:
    case R_LARCH_ADD64:
    case R_LARCH_SUB8:
    case R_LARCH_SUB16:
    case R_LARCH_SUB24:
    case R_LARCH_SUB32:
    case R_LARCH_SUB64:
    case R_LARCH_32:
    case R_LARCH_64:
    case R_LARCH_32_PCREL:
        return 0;
    }
    return -1;
}

/* Returns an enumerator to describe whether and when the relocation needs a
   GOT and/or PLT entry to be created. */
ST_FUNC int gotplt_entry_type (int reloc_type)
{
    switch (reloc_type) {
    case R_LARCH_NONE:
    case R_LARCH_RELATIVE:
    case R_LARCH_COPY:
    case R_LARCH_JUMP_SLOT:
    case R_LARCH_IRELATIVE:
    case R_LARCH_ADD8:
    case R_LARCH_ADD16:
    case R_LARCH_ADD24:
    case R_LARCH_ADD32:
    case R_LARCH_ADD64:
    case R_LARCH_SUB8:
    case R_LARCH_SUB16:
    case R_LARCH_SUB24:
    case R_LARCH_SUB32:
    case R_LARCH_SUB64:
    case R_LARCH_DELETE:   /* relax 标记: 不需要 GOT/PLT */
    case R_LARCH_CFA:
    case R_LARCH_ALIGN:
    case R_LARCH_TLS_LE_HI20:  /* 局部执行模型 TLS: 无需 GOT
                                  (relocate() 暂无实现, 命中会打印 FIXME) */
    case R_LARCH_TLS_LE_LO12:
        return NO_GOTPLT_ENTRY;

    case R_LARCH_B16:
    case R_LARCH_B21:
    case R_LARCH_B26:
    case R_LARCH_PCALA_HI20:
    case R_LARCH_PCALA_LO12:
    case R_LARCH_PCALA64_LO12:
    case R_LARCH_PCALA64_HI20:
    case R_LARCH_ABS_HI20:
    case R_LARCH_ABS_LO12:
    case R_LARCH_ABS64_LO20:
    case R_LARCH_ABS64_HI12:
    case R_LARCH_PCALA64_LO20:
    case R_LARCH_PCALA64_HI12:
    case R_LARCH_32:
    case R_LARCH_64:
    case R_LARCH_32_PCREL:
        return AUTO_GOTPLT_ENTRY;

    case R_LARCH_GOT_HI20:
    case R_LARCH_GOT_LO12:
    case R_LARCH_GOT_PC_HI20:
    case R_LARCH_GOT_PC_LO12:
        return ALWAYS_GOTPLT_ENTRY;
    }
    return -1;
}

ST_FUNC unsigned create_plt_entry(TCCState *s1, unsigned got_offset, struct sym_attr *attr)
{
    Section *plt = s1->plt;
    uint8_t *p;
    unsigned plt_offset;

    if (plt->data_offset == 0)
        section_ptr_add(plt, 32);
    plt_offset = plt->data_offset;

    p = section_ptr_add(plt, 16);
    write64le(p, got_offset);
    return plt_offset;
}

/* relocate the PLT */
ST_FUNC void relocate_plt(TCCState *s1)
{
    uint8_t *p, *p_end;

    if (!s1->plt)
      return;

    p = s1->plt->data;
    p_end = p + s1->plt->data_offset;

    /* Minimal PLT: not used for static RmikuOS programs.  Fill with
       pcalau12i+ld.d+jirl placeholder so it at least links. */
    while (p < p_end) {
        write32le(p, 0x16000000u);  /* pcalau12i $t0, 0 */
        write32le(p + 4, 0x28c0058cu); /* ld.d $t0, $t0, 0 */
        write32le(p + 8, 0x4c000180u); /* jirl $zero, $t0, 0 */
        write32le(p + 12, 0x03400000u); /* nop */
        p += 16;
    }

    if (s1->plt->reloc) {
        ElfW_Rel *rel;
        p = s1->got->data;
        for_each_elem(s1->plt->reloc, 0, rel, ElfW_Rel) {
            write64le(p + rel->r_offset, s1->plt->sh_addr);
        }
    }
}

ST_FUNC void relocate(TCCState *s1, ElfW_Rel *rel, int type, unsigned char *ptr,
              addr_t addr, addr_t val)
{
    uint64_t off64;
    uint32_t off32;
    int sym_index = ELFW(R_SYM)(rel->r_info);

    switch(type) {
    case R_LARCH_DELETE:   /* relax 标记: 不做任何事 */
    case R_LARCH_CFA:
    case R_LARCH_ALIGN:
        return;
    case R_LARCH_ADD8:
        *ptr += val;
        return;
    case R_LARCH_ADD16:
        write16le(ptr, read16le(ptr) + val);
        return;
    case R_LARCH_ADD24:
        write32le(ptr, (read32le(ptr) & 0xffffff) + val);
        return;
    case R_LARCH_ADD32:
        write32le(ptr, read32le(ptr) + val);
        return;
    case R_LARCH_ADD64:
        write64le(ptr, read64le(ptr) + val);
        return;
    case R_LARCH_SUB8:
        *ptr -= val;
        return;
    case R_LARCH_SUB16:
        write16le(ptr, read16le(ptr) - val);
        return;
    case R_LARCH_SUB24:
        write32le(ptr, (read32le(ptr) & 0xffffff) - val);
        return;
    case R_LARCH_SUB32:
        write32le(ptr, read32le(ptr) - val);
        return;
    case R_LARCH_SUB64:
        write64le(ptr, read64le(ptr) - val);
        return;

    case R_LARCH_B26: {
        /* b/bl: si26 = (S+A-PC)>>2; bits[25:10]=si26[15:0], bits[9:0]=si26[25:16]
           (原实现把两个字段的移位写反了: 高 10 位误 <<10 到 [19:10],
            低 16 位忘了 <<10 到 [25:10]) */
        int64_t off = val - addr;
        uint32_t insn = read32le(ptr);
        if ((off + (1 << 27)) & ~(((uint64_t)1 << 28) - 4))
            tcc_error_noabort("R_LARCH_B26 relocation failed (val=%lx addr=%lx)", (long)val, (long)addr);
        insn = (insn & 0xfc000000u) | (((off >> 2) & 0xffff) << 10)
               | ((off >> 18) & 0x3ff);
        write32le(ptr, insn);
        return;
    }
    case R_LARCH_B21: {
        /* beqz/bnez: si21 = (S+A-PC)>>2; bits[25:10]=si21[15:0], bits[4:0]=si21[20:16],
           bits[9:5]=rj 必须保留(原实现 mask 0xfc000000 把 rj 清零) */
        int64_t off = val - addr;
        uint32_t insn = read32le(ptr);
        if ((off + (1 << 22)) & ~(((uint64_t)1 << 23) - 4))
            tcc_error_noabort("R_LARCH_B21 relocation failed");
        insn = (insn & 0xfc0003e0u) | (((off >> 2) & 0xffff) << 10)
               | ((off >> 18) & 0x1f);
        write32le(ptr, insn);
        return;
    }
    case R_LARCH_B16: {
        /* beq/bne/blt/...: si16 = (S+A-PC)>>2, 在 bits[25:10];
           bits[9:5]=rj, bits[4:0]=rd 必须保留(原实现 mask 0xfc000000 把两者清零) */
        int64_t off = val - addr;
        uint32_t insn = read32le(ptr);
        if ((off + (1 << 17)) & ~(((uint64_t)1 << 18) - 4))
            tcc_error_noabort("R_LARCH_B16 relocation failed");
        insn = (insn & 0xfc0003ffu) | (((off >> 2) & 0xffff) << 10);
        write32le(ptr, insn);
        return;
    }

    case R_LARCH_PCALA_HI20: {
        pcala_hi20:
        /* psABI: imm20 = (((S+A+0x800) & ~0xfff) - (P & ~0xfff)) >> 12
           pcalau12i 是页对齐语义: rd = (PC & ~0xfff) + sext20(imm20)<<12,
           与 LO12 的 addi.d 符号扩展配套后对任意 P 正确。
           原实现 (val&~0xfff)-(addr&~0xfff) 缺 +0x800 进位,
           符号地址低 12 位 >= 0x800 时结果差 4KB。 */
        int64_t off = ((val + 0x800) & ~(int64_t)0xfff) - (addr & ~(int64_t)0xfff);
        uint32_t insn = read32le(ptr);
        off >>= 12;  /* 算术右移 */
        if (off > 0x7ffff || off < -0x80000)
            tcc_error_noabort("R_LARCH_PCALA_HI20 relocation failed (off=%lx)", (long)off);
        insn = (insn & 0xfc00001fu) | ((((uint32_t)off) & 0xfffff) << 5);
        write32le(ptr, insn);
        return;
    }
    case R_LARCH_PCALA_LO12: {
        pcala_lo12:
        /* psABI: imm12 = (S+A) & 0xfff —— 不减 P!
           pcalau12i 页对齐语义下, addi.d 对 imm12 的符号扩展
           正好补偿 HI20 的 +0x800 进位。 */
        uint32_t insn = read32le(ptr);
        insn = (insn & 0xffc003ffu) | ((val & 0xfff) << 10);
        write32le(ptr, insn);
        return;
    }
    case R_LARCH_PCALA64_LO12: /* 105: GCC14+ 可松弛形式, 语义同 PCALA_LO12 */
        goto pcala_lo12;
    case R_LARCH_PCALA64_HI20: /* 106: 同 PCALA_HI20 */
        goto pcala_hi20;
    case R_LARCH_ABS_HI20: {
        /* lu12i.w+addi.w 配对: imm20 = ((S+A+0x800) >> 12) & 0xfffff
           (原实现缺 +0x800, 低 12 位 >= 0x800 的绝对地址全错 4KB) */
        uint32_t insn = read32le(ptr);
        insn = (insn & 0xfc00001fu) | ((((uint32_t)(val + 0x800) >> 12) & 0xfffff) << 5);
        write32le(ptr, insn);
        return;
    }
    case R_LARCH_ABS_LO12: {
        uint32_t insn = read32le(ptr);
        insn = (insn & 0xffc003ffu) | ((val & 0xfff) << 10);
        write32le(ptr, insn);
        return;
    }
    case R_LARCH_ABS64_LO20: {
        /* lu32i.d: imm20@[24:5] = ((S+A+0x800) >> 32) & 0xfffff (+0x800 的
           进位可能越过 bit32, 必须包含) */
        uint32_t insn = read32le(ptr);
        insn = (insn & 0xfc00001fu) | ((((uint64_t)(val + 0x800) >> 32) & 0xfffff) << 5);
        write32le(ptr, insn);
        return;
    }
    case R_LARCH_ABS64_HI12: {
        /* lu52i.d (2RI12): si12@[21:10] = ((S+A+0x800) >> 52) & 0xfff */
        uint32_t insn = read32le(ptr);
        insn = (insn & 0xffc003ffu) | ((((uint64_t)(val + 0x800) >> 52) & 0xfff) << 10);
        write32le(ptr, insn);
        return;
    }
    case R_LARCH_PCALA64_LO20: {
        /* lu32i.d: 绝对"调整页"((S+A+0x800)&~0xfff)的 bits[51:32]
           (pcalau12i 只保证低 32 位页正确, lu32i/lu52i 覆写高位) */
        int64_t page = (val + 0x800) & ~(int64_t)0xfff;
        uint32_t insn = read32le(ptr);
        insn = (insn & 0xfc00001fu) | ((((uint64_t)page >> 32) & 0xfffff) << 5);
        write32le(ptr, insn);
        return;
    }
    case R_LARCH_PCALA64_HI12: {
        int64_t page = (val + 0x800) & ~(int64_t)0xfff;
        uint32_t insn = read32le(ptr);
        insn = (insn & 0xffc003ffu) | ((((uint64_t)page >> 52) & 0xfff) << 10);
        write32le(ptr, insn);
        return;
    }
    case R_LARCH_GOT_PC_HI20: {
        /* val 是符号地址, 但 pcalau12i+ld.d 需要的是 GOT 槽地址!
           (对照 riscv64-link.c: val = got->sh_addr + got_offset;
            原实现直接填符号地址 -> ld.d 从函数入口读出两条指令当指针,
            jirl 跳到"指令对"拼成的野地址 -> ADEF, 这是全线崩溃主因)
           公式同 PCALA_HI20(含 +0x800, ld.d 的 imm12 符号扩展补偿) */
        addr_t got = s1->got->sh_addr + get_sym_attr(s1, sym_index, 0)->got_offset;
        int64_t off = ((got + 0x800) & ~(int64_t)0xfff) - (addr & ~(int64_t)0xfff);
        uint32_t insn = read32le(ptr);
        insn = (insn & 0xfc00001fu) | ((((uint32_t)(off >> 12)) & 0xfffff) << 5);
        write32le(ptr, insn);
        return;
    }
    case R_LARCH_GOT_PC_LO12: {
        /* GOT 槽地址低 12 位(不减 P, 页对齐语义) */
        addr_t got = s1->got->sh_addr + get_sym_attr(s1, sym_index, 0)->got_offset;
        uint32_t insn = read32le(ptr);
        insn = (insn & 0xffc003ffu) | ((got & 0xfff) << 10);
        write32le(ptr, insn);
        return;
    }
    case R_LARCH_GOT_HI20: {
        /* 79: 绝对形式 lu12i.w + ld.d, 同样用 GOT 槽地址 */
        addr_t got = s1->got->sh_addr + get_sym_attr(s1, sym_index, 0)->got_offset;
        uint32_t insn = read32le(ptr);
        insn = (insn & 0xfc00001fu) | ((((uint64_t)(got + 0x800) >> 12) & 0xfffff) << 5);
        write32le(ptr, insn);
        return;
    }
    case R_LARCH_GOT_LO12: {
        addr_t got = s1->got->sh_addr + get_sym_attr(s1, sym_index, 0)->got_offset;
        uint32_t insn = read32le(ptr);
        insn = (insn & 0xffc003ffu) | ((got & 0xfff) << 10);
        write32le(ptr, insn);
        return;
    }
    case R_LARCH_32:
        if (s1->output_type & TCC_OUTPUT_DYN) {
            qrel->r_offset = rel->r_offset;
            qrel->r_info = ELFW(R_INFO)(0, R_LARCH_RELATIVE);
            qrel->r_addend = (int)read32le(ptr) + val;
            qrel++;
        }
        add32le(ptr, val);
        return;
    case R_LARCH_64:
        if (s1->output_type & TCC_OUTPUT_DYN) {
            qrel->r_offset = rel->r_offset;
            qrel->r_info = ELFW(R_INFO)(0, R_LARCH_RELATIVE);
            qrel->r_addend = read64le(ptr) + val;
            qrel++;
        }
    case R_LARCH_JUMP_SLOT:
        add64le(ptr, val);
        return;
    case R_LARCH_32_PCREL:
        add32le(ptr, val - addr);
        return;
    case R_LARCH_COPY:
        return;
    case R_LARCH_RELATIVE:
        return;

    default:
        fprintf(stderr, "FIXME: handle reloc type %x at %x [%p] to %x\n",
                type, (unsigned)addr, ptr, (unsigned)val);
        return;
    }
}
#endif
