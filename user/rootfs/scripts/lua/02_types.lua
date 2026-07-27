-- 02_types.lua  类型系统 + table
-- 用法: lua 02_types.lua

-- type()
print(type(nil))       -- nil
print(type(true))      -- boolean
print(type(42))        -- number
print(type("hi"))      -- string
print(type({}))        -- table
print(type(print))     -- function

-- math.type() 区分整数/浮点（Lua 5.4）
print(math.type(1))     -- integer
print(math.type(1.0))   -- float

-- 数组 table
local arr = {10, 20, 30, 40, 50}
print("len:", #arr)
local sum = 0
for i, v in ipairs(arr) do
    sum = sum + v
end
print("sum:", sum)      -- 150

-- hash table
local info = {name="rmiku", arch="riscv64", year=2026}
for k, v in pairs(info) do
    print(" ", k, "=", v)
end

-- 嵌套 + 修改
local t = {a={b={c=1}}}
t.a.b.c = 42
print("nested:", t.a.b.c)   -- 42

-- table 库
table.insert(arr, 60)
print("after insert:", arr[#arr])   -- 60
table.remove(arr, 1)
print("after remove first:", arr[1], "len:", #arr)  -- 20  5

-- table.concat
print("join:", table.concat({"a","b","c"}, "-"))   -- a-b-c

--[[
关键检查点:
- type() 返回正确字符串
- math.type() 区分 integer/float（Lua 5.4 特性）
- ipairs 顺序遍历数组, pairs 遍历 hash（顺序不保证）
- table.insert/remove/concat 正常
]]
