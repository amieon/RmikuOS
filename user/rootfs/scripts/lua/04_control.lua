-- 04_control.lua  控制流 + 迭代器
-- 用法: lua 04_control.lua

-- if / elseif / else
local function grade(score)
    if score >= 90 then return "A"
    elseif score >= 80 then return "B"
    elseif score >= 70 then return "C"
    elseif score >= 60 then return "D"
    else return "F"
    end
end
print("95 ->", grade(95))   -- A
print("72 ->", grade(72))   -- C
print("40 ->", grade(40))   -- F

-- while
local i, s = 1, 0
while i <= 100 do
    s = s + i
    i = i + 1
end
print("while sum 1..100:", s)   -- 5050

-- repeat-until
local n, fact = 1, 1
repeat
    fact = fact * n
    n = n + 1
until n > 5
print("5! =", fact)   -- 120

-- numeric for（含负步长）
for i = 10, 1, -1 do
    if i == 1 then print("countdown:", i) end
end

-- generic for + 自定义迭代器
local function range(a, b)
    local i = a - 1
    return function()
        i = i + 1
        if i <= b then return i end
    end
end

local product = 1
for x in range(1, 5) do
    product = product * x
end
print("product 1..5:", product)   -- 120

-- break
for i = 1, 100 do
    if i * i > 50 then
        print("first i with i^2>50:", i)   -- 8
        break
    end
end

--[[
关键检查点:
- if/elseif/else 分支正确
- while / repeat-until / numeric for / generic for 全部正常
- 自定义迭代器（闭包）工作
- break 跳出循环
]]
