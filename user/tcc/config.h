/* ============================================================================
 * config.h —— RmikuOS 用的 TCC 手工配置（仿 configure 生成物）
 *
 * 关键决策：
 *   - TCC_TARGET_RISCV64      选 riscv64 后端
 *   - CONFIG_TCC_ELFINTERP "" 不写 PT_INTERP（内核 loader 拒绝动态链接段）
 *   - CONFIG_TCC_SEMLOCK 0    RmikuOS 无 sem/pthread 锁, 单线程
 *   - CONFIG_TCC_STATIC 1     静态
 *   - 系统路径全空：无系统 include/库/crt（用 RmikuOS 的 user/include + crt0）
 * ==========================================================================*/
#ifndef CONFIG_H
#define CONFIG_H

#define TCC_VERSION "0.9.28"

/* ---- 目标架构 ---- */
#define TCC_TARGET_RISCV64 1

/* ---- 路径配置：RmikuOS 无系统目录, 全空 ---- */
#define CONFIG_TCC_ELFINTERP ""
#define CONFIG_TCC_SYSINCLUDEPATHS ""
#define CONFIG_TCC_LIBPATHS ""
#define CONFIG_TCC_CRTPREFIX ""
#define CONFIG_TCCDIR ""
#define CONFIG_TCC_CROSSPREFIX ""

/* ---- 功能开关 ---- */
#define CONFIG_TCC_BCHECK 0
#define CONFIG_TCC_BACKTRACE 0
#define CONFIG_TCC_PIE 0
#define CONFIG_TCC_PIC 0
#define CONFIG_TCC_SEMLOCK 0        /* RmikuOS 无信号量锁 */
#define CONFIG_TCC_STATIC 1
#define CONFIG_TCC_PREDEFS 1
#define CONFIG_TCC_ASM 1
#define CONFIG_TCC_ELFINTERP_ARMHF ""

#endif /* CONFIG_H */
