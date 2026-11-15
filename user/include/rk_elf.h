#pragma once
/*
 * ELF64 的最小只读视图 —— 给用户态 mini loader(user/lib/dl.c) 用。
 *
 * 为什么不叫 elf.h：TCC 的 tcc-src/tcc.h 里有 `#include "elf.h"`，而用户态
 * 编译时 -I user/include 排在最前。占住标准名会让第三方静默换一套结构定义，
 * 编译能过、字段全错 —— 正是本项目反复踩过的形状。所以类型/常量一律 rk_ 前缀。
 *
 * 所有 64 位字段用 unsigned long long，不用 usize / long：
 * 本项目 usize = unsigned long，在宿主(LLP64)是 4 字节、真机(LP64)是 8 字节。
 * ELF64 的偏移/地址恒为 8 字节，用会变宽的类型等于把同一个 .so 解析成两种布局。
 * 结构体靠自然对齐，sizeof 依次是 64 / 56 / 16 / 24 / 24，和 ELF gABI 一致。
 */
#ifdef __cplusplus
extern "C" {
#endif

/* ---- e_ident 下标 ---- */
#define RK_EI_CLASS   4
#define RK_EI_DATA    5
#define RK_EI_VERSION 6
#define RK_ELFCLASS64 2
#define RK_ELFDATA2LSB 1
#define RK_EV_CURRENT 1
/* 小端下 "\x7fELF" 的头 4 字节按 u32 读出来就是这个值 */
#define RK_ELF_MAGIC32 0x464c457fu

#define RK_ET_EXEC 2
#define RK_ET_DYN  3

#define RK_EM_RISCV     243
#define RK_EM_LOONGARCH 258

#define RK_PT_LOAD    1
#define RK_PT_DYNAMIC 2
#define RK_PT_INTERP  3
#define RK_PF_X 1
#define RK_PF_W 2
#define RK_PF_R 4

/* ---- 动态段 tag（只列 loader 会判断的；数值照 ELF gABI / Linux） ---- */
#define RK_DT_NULL          0
#define RK_DT_NEEDED        1
#define RK_DT_PLTRELSZ      2
#define RK_DT_PLTGOT        3
#define RK_DT_HASH          4
#define RK_DT_STRTAB        5
#define RK_DT_SYMTAB        6
#define RK_DT_RELA          7
#define RK_DT_RELASZ        8
#define RK_DT_RELAENT       9
#define RK_DT_STRSZ         10
#define RK_DT_SYMENT        11
#define RK_DT_INIT          12
#define RK_DT_FINI          13
#define RK_DT_SONAME        14
#define RK_DT_SYMBOLIC      16
#define RK_DT_REL           17
#define RK_DT_RELSZ         18
#define RK_DT_RELENT        19
#define RK_DT_PLTREL        20
#define RK_DT_TEXTREL       22
#define RK_DT_JMPREL        23
#define RK_DT_BIND_NOW      24
#define RK_DT_INIT_ARRAY    25
#define RK_DT_FINI_ARRAY    26
#define RK_DT_INIT_ARRAYSZ  27
#define RK_DT_FINI_ARRAYSZ  28
#define RK_DT_FLAGS         30
#define RK_DT_GNU_HASH      0x6ffffef5u
#define RK_DT_VERSYM        0x6ffffff0u
#define RK_DT_RELACOUNT     0x6ffffff9u
#define RK_DT_VERDEF        0x6ffffffcu
#define RK_DT_VERNEED       0x6ffffffeu

/* ---- 符号 ---- */
#define RK_STB_LOCAL  0
#define RK_STB_GLOBAL 1
#define RK_STB_WEAK   2
#define RK_STB(i)     ((unsigned char)((i) >> 4))
#define RK_STT(i)     ((unsigned char)((i) & 0xf))
#define RK_SHN_UNDEF  0
#define RK_SHN_ABS    0xfff1u

#define RK_ELF64_R_SYM(i)  ((unsigned int)((i) >> 32))
#define RK_ELF64_R_TYPE(i) ((unsigned int)((i) & 0xffffffffull))

/* ---- 重定位类型：两套 psABI 分开列，故意不合并 ----
 * 0..5 的数值两边碰巧一样，但 IRELATIVE 不同（RISC-V 58 / LoongArch 12），
 * 静态重定位那一大坨更是完全不同。合成一套「通用」switch 迟早会把某个
 * LoongArch 类型当成 RISC-V 的 PCREL 处理掉。TCC 自己也是分两个文件写的。 */
#define RK_R_RISCV_NONE       0
#define RK_R_RISCV_32         1
#define RK_R_RISCV_64         2
#define RK_R_RISCV_RELATIVE   3
#define RK_R_RISCV_COPY       4
#define RK_R_RISCV_JUMP_SLOT  5
#define RK_R_RISCV_IRELATIVE  58

#define RK_R_LARCH_NONE       0
#define RK_R_LARCH_32         1
#define RK_R_LARCH_64         2   /* LoongArch 没有 GLOB_DAT，GOT 数据项就是 64 */
#define RK_R_LARCH_RELATIVE   3
#define RK_R_LARCH_COPY       4
#define RK_R_LARCH_JUMP_SLOT  5
#define RK_R_LARCH_IRELATIVE  12

typedef unsigned long long rk_elf64_addr;
typedef unsigned long long rk_elf64_off;
typedef long long          rk_elf64_sxword;

typedef struct {
    unsigned char  e_ident[16];
    unsigned short e_type;
    unsigned short e_machine;
    unsigned int   e_version;
    unsigned long long e_entry;
    unsigned long long e_phoff;
    unsigned long long e_shoff;
    unsigned int   e_flags;
    unsigned short e_ehsize;
    unsigned short e_phentsize;
    unsigned short e_phnum;
    unsigned short e_shentsize;
    unsigned short e_shnum;
    unsigned short e_shstrndx;
} rk_elf64_ehdr;                 /* 64 bytes */

typedef struct {
    unsigned int       p_type;
    unsigned int       p_flags;
    unsigned long long p_offset;
    unsigned long long p_vaddr;
    unsigned long long p_paddr;
    unsigned long long p_filesz;
    unsigned long long p_memsz;
    unsigned long long p_align;
} rk_elf64_phdr;                 /* 56 bytes */

typedef struct {
    long long          d_tag;
    unsigned long long d_val;
} rk_elf64_dyn;                  /* 16 bytes */

typedef struct {
    unsigned int       st_name;
    unsigned char      st_info;
    unsigned char      st_other;
    unsigned short     st_shndx;
    unsigned long long st_value;
    unsigned long long st_size;
} rk_elf64_sym;                  /* 24 bytes */

typedef struct {
    unsigned long long r_offset;
    unsigned long long r_info;
    long long          r_addend;
} rk_elf64_rela;                 /* 24 bytes */

#ifdef __cplusplus
}
#endif
