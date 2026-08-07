[← 返回 RmikuOS 主页](../README.md)

---

## User Programs and Shell

RmikuOS 从 ext4 rootfs 中加载用户程序，第一个用户进程（init shell）通过 VFS 从 `/bin/shell` 加载。Shell 不是内核内置的玩具解释器，而是一个**支持交互式编辑、通配符展开、逻辑链、后台执行与脚本 source** 的完整用户态程序。

### Shell 命令体系

区分**内建命令**（改变 shell 自身状态，必须内建）与**外部命令**（独立 ELF，可被管道 / 重定向组合）：

```text
内建：  cd  pwd  exit  help  shutdown  jobs  clear
        mkdir  touch  rm  rmdir  mv  rename  source  .
        export  env  unset
外部：  ls  cat  echo  grep  shell  sleep ...
```

`ls` / `cat` / `echo` / `grep` 等 IO 工具被实现为独立的外部程序，因此它们能出现在管道与重定向中；`cd` / `pwd` / `exit` / `jobs` / `source` / `export` / `env` / `unset` 保持内建，因为它们必须修改 shell 进程自身的状态（如环境变量表、当前工作目录）或访问 shell 内部数据结构。

### Interactive Line Editing

Shell 支持完整的命令行编辑，无需依赖 readline 库：

```text
↑ / ↓         浏览历史命令（环形缓冲区，自动去重）
← / →         光标左右移动，支持任意位置插入与删除
Backspace     在光标处删除字符（自动重绘后续内容）
Tab           命令补全 + 路径补全
```

**Tab 补全**：

- 唯一匹配时直接补全，命令后自动追加空格；
- 多匹配时先补**最长公共前缀**（LCP），再次按 Tab 列出候选列表；
- 支持绝对路径与相对路径（`ls /bi<Tab>` → `ls /bin/`）。

### Glob, Brace Expansion & Quoting

Shell 在参数展开阶段实现了**大括号展开 → 通配符展开**的两级展开，且引号内完全保护：

```text
# 通配符
/ $ ls *.c
/ $ ls /bin/s?ell
/ $ rm [abc]*.o

# 大括号展开
/ $ echo {hello,world}
hello world
/ $ echo file{1,2,3}.txt
file1.txt file2.txt file3.txt
/ $ echo num{10..13}
num10 num11 num12 num13

# 引号保护（内部不做任何展开）
/ $ echo "*.c"
*.c
/ $ echo 'hello * ? [abc]'
hello * ? [abc]
```

**字符类** `[abc]`、`[a-z]`、`[^abc]`（或 `[!abc]`）也支持，与 `*` `?` 自由组合。

### Control Flow: `;` `&&` `||` `&`

Shell 支持完整的命令控制流：

```text
;           顺序执行（多条命令分隔）
&&          短路与（前一条成功才执行后一条）
||          短路或（前一条失败才执行后一条）
&           后台执行（不阻塞 shell，返回 job id）
```

```text
/ $ cd /bin && ls | grep shell || echo not found
/ $ echo one; echo two; echo three
/ $ sleep 5 &
[1] 3
/ $ jobs
[1] running sleep
/ $ ...（5秒后自动打印）[1] done sleep
```

`&&` / `||` 与管道 `|` 的优先级关系与 bash 一致：管道先组合命令，再参与逻辑短路。

### Script Execution: `source` / `.`

Shell 支持从文件逐行执行脚本，实现为内建命令 `source` 或 `.`：

```text
/ $ cat test.sh
echo "========== hello =========="
echo {1,2,3}
ls /bin/*.c 2>/dev/null || echo no .c files
cd /tmp && touch foo && ls foo
/ $ source test.sh
========== hello ==========
1 2 3
no .c files
foo
```

支持**续行**（行尾 `\` 将下一行拼接），支持 `#` 行内注释。脚本中可任意使用管道、重定向、逻辑链与后台执行。

### Pipeline & Redirection

`pipe()` 创建匿名管道，`dup2` 实现重定向。Shell 支持：

```text
cmd > file        stdout 覆盖写入
cmd >> file       stdout 追加
cmd < file        stdin 从文件读取
cmd1 | cmd2       单级管道
cmd1 | cmd2 | ... 多级管道
cmd < in | f1 | f2 > out   管道 + 两端重定向
```

管道与重定向的解析在**引号保护**下进行：`echo "a > b | c"` 被正确当作单个字面参数，不会触发重定向或管道。

### Lexing（词法解析）

命令行解析为原地压缩的词法分析器，单遍扫描完成：

```text
"..." / '...'   引号剥离；双引号内处理 \ 转义，单引号内全字面
\               转义：反斜杠后的字符按字面保留
#               行内注释：词首的 # 起至行尾忽略
```

引号状态在分词、管道切分、重定向解析三处一致跟踪，保证操作符在引号内无特殊含义。



---

### Environment Variables & `$?` Expansion

Shell 支持用户态环境变量与 `$?`（上一条命令退出码）的读取与修改，并内建 `export` / `env` / `unset` 三个命令：

```text
/ $ export FOO=bar            # 设置环境变量（shell 进程内，exec 子进程时随 envp 透传）
/ $ echo $FOO                 # 单参数展开 -> bar
/ $ echo ${FOO}               # 大括号形式 -> bar
/ $ echo "FOO=$FOO"           # 双引号内展开 -> FOO=bar
/ $ echo '$FOO'               # 单引号保护 -> $FOO（不展开）
/ $ ls /bin/*.c; echo $?      # $? = 上一条命令退出码
/ $ env                      # 打印当前全部 KEY=VALUE
/ $ unset FOO                # 删除变量
```

展开规则与 bash 对齐：

* `$VAR` / `${VAR}`：读取环境变量（经 `getenv` syscall）；
* `$?` / `${?}`：展开为 `g_last_exit`（每跑完一条命令由 `execute_line` 更新，含管道整条命令的退出码）；
* **单引号** `'...'` 内完全字面，**不展开**任何 `$`；
* **双引号** `"..."` 内展开 `$` 与转义 `\`，但保留空白与字边界；
* 变量展开在引号保护下进行：`echo "$FOO | bar"` 不会被误判为管道。

`export` 设置的环境变量在 shell `exec` 外部命令时，随内核经寄存器传入的 `envp` 透传给子进程（详见下节「Environment Variable Syscalls」）。shell 的命令搜索目录也从环境变量 `PATH`（`getenv("PATH")`，按 `:` 切分，回退 `/bin /tests /programs`）读取，因此 `export PATH=/bin:/tests` 等修改即时生效。

---

### TCC：系统内的 C 编译器（自托管工具链）

TCC（Tiny C Compiler）0.9.28 移植——RmikuOS 能**在自己的系统里现场编译并运行 C 程序**，形成「内核写 C → 系统里编译 C → 运行 C」的完整自托管闭环。

#### 用法

```
tcc hello.c -o hello && ./hello    # AOT: 编译出 ELF, 内核加载执行
tcc hello.c -run                   # JIT: 编译进内存直接执行
```

`/codes/` 内置 12 个测试程序（镜像 overlay 带进），`scripts/tcc_test.sh` 顺序执行——**12/12 全过**。

#### 系统文件布局（`/usr/lib/tcc`）

| 文件 | 作用 |
|---|---|
| `crt1.o` / `crti.o` / `crtn.o` | 启动（复用 crt0，`_start`→main→exit）/ 空 .init/.fini |
| `libc.a` | syscall.o + string.o + **libc_extern.o** |
| `libtcc1.a` | TCC 运行时 + **libgcc 软浮点合并**（long double 等） |
| `runmain.o` | `-run`（JIT）启动 stub |
| `include/` | RmikuOS 头 + TCC 编译器配套头（stddef/stdarg/stdbool…） |

#### 三个关键设计

**① libc 具现化（libc_extern.c）**：RmikuOS 的 libc 全是**头内联 static inline**（无真实符号），而 TCC 对 `static inline` 不内联、生成外部引用 → `unresolved printf`。解法：把全部头内联函数重命名（`__rmiku_*`）后 include（宿主 gcc 内联消化内部依赖链），再为公共 API 生成非 static 转发打进 libc.a。顺序必须是「**重命名 → include → undef → 转发**」——include 放重命名前会 redefinition。

**② libgcc 全量合并**：TCC 静态链接**不自动链 libgcc**（`TCC_LIBGCC` 只在动态链接生效）→ long double（128 位 quad）软浮点 `__*tf*`、`__clzdi2` 等全部 unresolved。解法：`add_libgcc_tf()` 把 host libgcc.a 全量展开、**与 libtcc1.o 去重**后合并进 libtcc1.a——TCC 惰性提取只取被引用的成员，多放无害，一次解决所有编译期辅助符号。

**③ 全局 `_stdout`（短输出不丢）**：标准流从「每编译单元一份 static」改为**全局唯一**（定义在 string.o）。否则 printf（某 TU）与 exit 的 flush（string.o）各用各的 `_stdout` 实例，flush 落空——短输出（<BUFSIZ）且无换行就丢失。crt0 调 main 后 `call exit`（flush 标准流再退出）。

#### 其他适配（踩坑实录）

- **静态 ELF**：libtcc.c `tcc_new()` 默认 `static_link=1`——否则产物带 `PT_INTERP`，内核 loader 拒绝；
- **weak 符号化解冲突**：runmain.o 自带强 `exit`/`atexit`（-run 专属）→ libc 侧用 `__attribute__((weak))` 让位；`.init_array`/`.fini_array` 边界符号 TCC 不生成 → 提供 weak 空数组，否则 `-run` reloc 静默失败；
- **mprotect 系统调用**（JIT 需要）：`PageTable::update_flags` 只改已有 PTE 的权限位，**不重映射、不分配新帧**——`map()` 是 assert 未映射语义，直接重映射会 panic；
- **open() 三参 mode 打通内核**：O_CREAT 创建文件后 `chmod(mode)`，POSIX 真语义（用户态变参 → syscall6 → 内核）；
- **`__sync_*` 原子符号**：lock.h 用 gcc 内建原子，TCC 不识别 → string.c 用 riscv64 `amoswap.w.aq`/loongarch64 `amswap_db.w` 提供真实符号；
- **stdint.h**：裸机工具链无 libc 版 → TCC 专用最小版（只进 `/usr/lib/tcc/include`，gcc 程序继续用编译器内置）。

#### 已知局限

- `-run` 走 runmain 的 exit，不 flush 无换行缓冲——`-run` 输出建议带 `
`；
- 用户程序显式 `exit()`（头内联）不 flush——次要路径；
- TCC 的 `static inline` 不内联是特性不是 bug，靠 libc_extern 转发兜底。

#### LoongArch64 后端：从「全崩」到 12/12 全绿

LoongArch64 后端的排障。起点极其狼狈：后端能编译、产物长得完全正常，但 12 个测试**一个都跑不起来**——所有程序在启动头几条指令就 SIGSEGV。难点在于：编译侧一切正确、运行时全军覆没。

**排障方法**：在宿主侧用同一份源码编出 `/tmp/tcc-la`，产出与镜像内**逐字节一致**的 ELF，再用 `objdump` 把每个 trap 的 `era` 精确映射到指令——整个调试过程就是「反汇编定罪 → 对照能用的 riscv64 后端 → 修 → 重测」的循环。

按时间线：

**① 指令编码层**：`pcalau12i` 的宏值写成了 `lu32i.d` 的、BEQZ 编码错、`jirl` 丢返回地址、r5/r6（其实是 `a1/a2` 参数寄存器）被当 scratch 用、B26/B21/B16 重定位字段移位错、PCALA_HI20 缺 `+0x800` 进位——**6 个编码错误叠在一起**。

**② 链接层**：`__extenddftf2` unresolved——根因是 libtcc1.o 的本地标签 `L0` 和每个 libgcc 成员「撞名」，build.py 的去重逻辑把**全部 tf 符号当重复项误杀**。诊断脚本实锤：libgcc 有 14 个 tf 符号，合并后的 libtcc1.a 是 0 个。

**③ 条件跳转层**：`gjmp_cond` 的条件**没取反**——riscv64 的写法是 `op ^ 1`（发反条件跳过链式跳转），移植时丢了。后果：**所有 if/while/for 的条件全部颠倒**，程序跑起来完全不是人写的逻辑。

**④ GOT 层**：`GOT_PC_HI20/LO12` 重定位直接填符号地址，而不是 **GOT 槽地址**（`got->sh_addr + got_offset`）→ `pcalau12i + ld.d` 从**函数入口读出前两条指令**当成函数指针 → `jirl` 跳进野地址。这正是所有 trap 里 `badv=指令对`（如 `0x28ffa2cc29ffa2c4` = 两条 `ld.d/st.d` 序言指令拼成的 64 位值）的来历。

**⑤ 重定位掩码层（主线崩溃根因）**：`relocate()` 的 imm20 保留掩码 `0xfc00001f` **错了一比特**——1RI20 格式指令（pcalau12i/lu12i.w/lu32i.d）的操作码是 **7 位**（bits[31:25]），掩码只保留 [31:26] 把 bit25 清了 → GAS 发出的 `pcalau12i $t0, 3`（`0x1a00000c`）链接后变成 `pcaddi $t0, 3`（`0x1800006c`）→ **PC 相对寻址全灭**。`lu12i.w` 因 bit25=0 恰好幸存，所以绝对寻址正常、PC 相对全崩——「什么都是对的但什么都跑不起来」的教科书案例。**era 恒定（崩在 crt1.o 内）+ badv 随构建变化（文本内容变）** 这两个特征直接把矛头钉死在重定位上。

**⑥ 变参层**：`tccdefs.h` 没有 LoongArch 分支 → `va_start/va_arg` 落进 **i386 的 4 字节槽方案** → `va_start(ap, fmt)` 变成 `ap = &fmt + 8`（= fp-16，恰好是保存旧 fp 的槽位）→ **所有 `%d` 打印同一个数 16764816**（= 初始 sp = 0xffcf90），`%f` 全 0，`%s` 全乱码。一个宏分支的缺失让整个 printf 看起来像坏了。

**⑦ 立即数层（全崩总根因）**：LoongArch 的 `ANDI/ORI/XORI` 立即数是**零扩展**（逻辑运算），`ADDI/SLTI` 是**符号扩展**——TCC 的立即数判断对两者一视同仁 → `& -4` 被编成 `andi $a0, $a0, 0xffc` → **64 位地址被截成 12 位小整数**（所有 `badv=0xexx` 的 PIL 崩溃）。RISC-V 的 `andi` 是符号扩展所以永远踩不到。`va_arg` 的 `_tcc_align`、malloc 的 `align_up`、一切 `& ~mask` 全中招。

**⑧ ABI 层**：浮点参数被放进了 `$f0-$f7`，而 LoongArch LP64D 的参数寄存器是 **`$f12-$f19`**（`$f0` 只是返回寄存器）。RISC-V 的 fa0-fa7 = f10-f17 与返回寄存器 f10 重合，移植代码「歪打正着」在 RISC-V 上自洽；LoongArch 必须显式 `fmov.d $f12+k ← $f(k)`。症状一眼看穿：`cos(0.0)=1.0` 对（$f12 初始恰好是 0）、`sqrt(2.0)=0`（libc 从 $f12 读到 0）。

**⑨ 常量求值层**：`tcc.h` 的 `CValue.i` 是 **uint64_t**，后端却用 `int fc = vtop->c.i` 截断 → `SLAB_MAGIC = 1ULL<<63` 变成 0 → `b->size = SLAB_MAGIC | sc` 被编成 `ori $a1, $a1, 0x0` → slab 头失去 magic → `realloc` 全部误判。定罪证据是反汇编里那行刺眼的 `ori 0x0`。RISC-V 版靠一行「废话」比较 `fc == vtop->c.i`（int 提升成 64 位比较）兜底，移植时丢了。

**方法论沉淀**（三句话）：

1. **反汇编是唯一真相**。`badv=指令对`、`era 恒定`、`ori 0x0`、`pcalau12i→pcaddi`——所有「看起来完全正常」的 bug，最后都是被 objdump 直接定罪，而不是靠读代码猜出来的。
2. **同树能用的后端是金矿**。GOT 槽地址、条件取反、freg 物理映射、64 位常量兜底——一半的修复是 `diff riscv64-gen.c` 直接对出来的。
3. **指令编码只信权威源**。binutils 的 loongarch-opc.c、QEMU 的 insns.decode、loongson 的 psABI 三源核对，宁可多查一次也不猜。

结局：**12/12 全绿**，与 RISC-V 参考输出逐字节一致——`tcc_test.sh` 里的每一行，都是这九层 bug 的墓志铭。

---

### kilo：全屏编辑器（与 TCC 组成开发闭环）

kilo（antirez 的经典教学编辑器，1308 行单文件 C）移植——与 TCC 配合，在 RmikuOS 内完成**「编辑 → 编译 → 运行」完整开发闭环**：

```
kilo hello.c          # 写代码（ANSI 全屏 + C 语法高亮）
tcc hello.c -o hello && ./hello    # 编译并运行
```

#### 适配要点（踩坑实录）

- **termios 最小模拟**（`include/termios.h`）：RmikuOS 无完整 termios，只把 `c_lflag` 的 `ECHO` 位映射到内核 `set_echo()`（raw 模式 = 关回显）；VMIN/VTIME 忽略（内核 read 天然逐字符）；`OPOST/CS8` 等标志只定义供位运算。
- **回车 = `\n`（ICRNL）**：内核 stdin read 把回车统一转成 `\n`（`stdio.rs` 的 ICRNL），kilo 原版只认 `\r` → 回车会被 insert 成裸 LF、屏幕错乱。修复：`case ENTER` 加 `case '\n'` 同判。
- **窗口固定 80x24**：无 ioctl(TIOCGWINSZ)/DSR 查询，`getWindowSize` 返回固定尺寸。
- **退出恢复终端**：RmikuOS crt0 的 exit 不跑 `atexit`，kilo 的 `atexit(editorAtExit)` 失效——Ctrl+Q/Ctrl+Z 退出前显式 `disableRawMode`。
- **Ctrl+S/Q 备选键**：Ctrl+S(0x13)/Ctrl+Q(0x11) 是 XON/XOFF 软件流控字符，QEMU 串口 + 宿主终端常吞 → 加备选 **Ctrl+X 保存 / Ctrl+Z 退出**（0x18/0x1A 不被流控占用）。
- 其他：`getline`→`fgets`（无 getline）、`ftruncate` 用真实实现（内核有 `sys_ftruncate`）、`isatty`/`__sync_*` 补 weak 符号。

#### 按键速查

| 键 | 功能 |
|---|---|
| 方向键 / PageUp / PageDown | 光标移动、翻页 |
| Enter | 换行（`\r`/`\n` 均识别） |
| Backspace / Del | 删除 |
| **Ctrl-S / Ctrl-X** | 保存 |
| **Ctrl-Q / Ctrl-Z** | 退出（未保存连按两次确认） |
| Ctrl-F | 搜索 |

---

### File System Map

![filesystem map](../docs/images/vfs.png)

