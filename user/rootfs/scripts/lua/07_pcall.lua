-- 07_pcall.lua  错误处理（setjmp/longjmp 核心压测）
-- 用法: lua 07_pcall.lua

-- 基本 pcall
local ok, err = pcall(function()
    error("something went wrong")
end)
print("pcall ok:", ok)         -- false
print("pcall err:", err)        -- ...something went wrong (error 会加文件位置前缀)

-- pcall 成功路径
local ok2, val = pcall(function()
    return 42
end)
print("pcall success:", ok2, val)   -- true  42

-- 多返回值
local ok3, a, b, c = pcall(function()
    return 1, 2, 3
end)
print("multi ret:", ok3, a, b, c)   -- true  1  2  3

-- 带参数调用
local function risky(n)
    if n < 0 then error("negative: " .. n) end
    if n > 100 then return "too big" end
    return n * 2
end

print(pcall(risky, 5))     -- true  10
print(pcall(risky, 150))   -- true  too big
print(pcall(risky, -1))    -- false  ...negative: -1

-- 错误后继续执行（验证 longjmp 不破坏栈）
for i = -1, 2 do
    local ok, result = pcall(risky, i)
    if ok then
        print(i, "->", result)
    else
        print(i, "error:", result)
    end
end
print("survived all errors")   -- 必须打印,证明 longjmp 后状态正常

-- 嵌套 pcall
local function outer()
    local ok, err = pcall(function()
        error("inner error")
    end)
    if not ok then
        error("rethrown: " .. err)
    end
    return "ok"
end
print(pcall(outer))   -- false  ...rethrown: ...inner error

-- assert
local ok4, err4 = pcall(function()
    assert(false, "assert message")
end)
print("assert:", ok4, err4)   -- false  ...assert message

-- error with level
local function deep()
    error("from deep", 2)   -- level 2: 错误归到调用者
end
local ok5, err5 = pcall(deep)
print("deep error:", ok5, err5)

--[[
关键检查点（全部依赖 setjmp/longjmp 正确）:
- pcall 捕获 error(), 返回 false + err
- error 默认加 "input:N: " 前缀（level=1）
- pcall 成功路径返回 true + 返回值
- 多返回值正确传递
- 错误后循环继续,不崩（longjmp 不破坏解释器状态）
- 嵌套 pcall + 重新 throw
- assert() 内部用 error()
- error(msg, level) 的 level 参数

如果这组全过,你的 setjmp/longjmp 实现基本没问题。
]]
