-- 01_hello.lua  基础输出 + 算术
-- 用法: lua 01_hello.lua
print("hello from lua on RmikuOS!")
print("lua version:", _VERSION)

-- 整数 vs 浮点（Lua 5.4 子类型）
print("1 + 2 =", 1 + 2)           -- 3 (integer)
print("3.14 * 2 =", 3.14 * 2)     -- 6.28 (float)
print("10 // 3 =", 10 // 3)       -- 3 (floor division, integer)
print("10 / 3 =", 10 / 3)         -- 3.3333... (true division, float)
print("2^10 =", 2^10)             -- 1024.0 (power always float)
print("7 % 3 =", 7 % 3)           -- 1 (modulo)

-- 字符串拼接
print("concat:", "hello" .. " " .. "lua" .. " " .. 2026)

--[[
预期输出:
hello from lua on RmikuOS!
lua version:	Lua 5.4
1 + 2 =	3
3.14 * 2 =	6.28
10 // 3 =	3
10 / 3 =	3.3333333333333
2^10 =	1024.0
7 % 3 =	1
concat:	hello lua 2026
]]
