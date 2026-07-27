-- 08_coroutine.lua  协程（setjmp/longjmp 协程切换压测）
-- 用法: lua 08_coroutine.lua

-- 基本协程
local co = coroutine.create(function(a, b)
    print("co start:", a, b)
    local r1 = coroutine.yield(a + b)
    print("co resumed:", r1)
    local r2 = coroutine.yield(r1 * 2)
    print("co resumed:", r2)
    return "done"
end)

print(coroutine.resume(co, 10, 20))   -- true  30
print(coroutine.resume(co, 5))        -- true  10
print(coroutine.resume(co, 99))       -- true  done
print("status:", coroutine.status(co))  -- dead

-- 生产者-消费者
local function producer()
    for i = 1, 5 do
        coroutine.yield(i * 10)
    end
end

local prod = coroutine.create(producer)
while coroutine.status(prod) ~= "dead" do
    local ok, val = coroutine.resume(prod)
    if val then print("produced:", val) end
end

-- 协程间双向通信（生成器）
local function gen_fib(n)
    return coroutine.wrap(function()
        local a, b = 0, 1
        for _ = 1, n do
            a, b = b, a + b
            coroutine.yield(a)
        end
    end)
end

local fibs = {}
for f in gen_fib(10) do
    table.insert(fibs, f)
end
print("fib:", table.concat(fibs, " "))
-- 1 1 2 3 5 8 13 21 34 55

-- 协程错误传播
local co2 = coroutine.create(function()
    error("boom inside coroutine")
end)
local ok, err = coroutine.resume(co2)
print("co error:", ok, err)   -- false  ...boom inside coroutine
print("co status:", coroutine.status(co2))  -- dead

-- coroutine.yield 在深层栈
local function deep_yield()
    local function level3()
        coroutine.yield("from level3")
    end
    level3()
    return "level2 done"
end

local co3 = coroutine.create(deep_yield)
print(coroutine.resume(co3))   -- true  from level3
print(coroutine.resume(co3))   -- true  level2 done

--[[
关键检查点:
- resume 传参 / yield 返回值双向通信
- coroutine.wrap 返回迭代器函数（比 create+resume 简洁）
- 协程内 error 被 resume 捕获,返回 false+err
- yield 在嵌套调用栈深处工作（压测 longjmp 跨栈帧）
- 协程结束后 status == "dead"

这组测试如果全过,说明你的 setjmp/longjmp 不仅支持 pcall,
还支持了协程的跨栈帧切换 —— 这是 Lua 5.4 最考验实现的特性。
]]
