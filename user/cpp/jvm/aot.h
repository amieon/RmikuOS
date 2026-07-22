#pragma once
#include "types.h"
#include "aot_common.h"

struct VM;

// ==================== 装载期 AOT ====================
// 类装载时把字节码模板式编译成本机机器码（RISC-V64 / LoongArch64）。
// 设计：
//  - locals / 操作数栈都是内存里的 8 字节槽，GC 语义和解释器一致
//  - 算术/分支/加载存储内联，其余 call 回 C++ helper
//  - GC 对 AOT 帧做保守扫描：槽值若在 heap.objects 里就当引用
//  - 不支持的方法整体 fallback 回解释器



// 编译单个方法，成功填充 m->aot_entry / m->aot_code_base
bool aot_compile_method(Method* m, ClassFile* cf);
// 类装载时调用：编译其全部方法（失败的标记走解释器）
void aot_compile_class(ClassFile* cf);
// 执行已编译方法
Value aot_exec(VM& vm, Method* m, ClassFile* cf, std::vector<Value>& args);