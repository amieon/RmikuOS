-- 03_closure.lua  闭包 + upvalue + 递归
-- 用法: lua 03_closure.lua

-- 计数器闭包（upvalue 捕获）
local function counter()
    local n = 0
    return function()
        n = n + 1
        return n
    end
end

local c = counter()
print(c(), c(), c())    -- 1  2  3

-- 多个闭包共享同一个 upvalue
local function make_pair()
    local shared = 0
    local function get() return shared end
    local function set(v) shared = v end
    return get, set
end

local get, set = make_pair()
set(100)
print("shared:", get())   -- 100
set(200)
print("shared:", get())   -- 200

-- 递归（factorial）
local function fact(n)
    if n <= 1 then return 1 end
    return n * fact(n - 1)
end
print("10! =", fact(10))   -- 3628800

-- 尾调用（Lua 支持尾调用优化）
local function tail_sum(n, acc)
    if n == 0 then return acc end
    return tail_sum(n - 1, acc + n)   -- 尾调用,不增长栈
end
print("sum 1..10000 =", tail_sum(10000, 0))   -- 50005000

-- 可变参数
local function sum_all(...)
    local s = 0
    for _, v in ipairs({...}) do s = s + v end
    return s
end
print("vararg:", sum_all(1, 2, 3, 4, 5))   -- 15

--[[
关键检查点:
- upvalue 正确捕获与修改（n 跨调用保留）
- 多闭包共享 upvalue（get/set 操作同一个 shared）
- 递归不爆栈（10! = 3628800）
- 尾调用优化（10000 层不爆栈,若没做 TCO 会 stack overflow）
- 可变参数 {...} 打包成 table
]]
