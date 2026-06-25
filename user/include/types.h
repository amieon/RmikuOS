#pragma once

/*
 * types.h —— 最底层类型定义。
 *
 * 整个用户态库的依赖根:所有其他头文件直接或间接依赖它。
 * 这里只放与平台无关的基础整数类型别名,不放任何逻辑。
 */

typedef unsigned long usize;
typedef long          isize;
