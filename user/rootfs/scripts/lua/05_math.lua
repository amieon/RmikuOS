-- 05_math.lua  数学库（压测你的裸运行时 math）
-- 用法: lua 05_math.lua

print("pi =", math.pi)
print("huge =", math.huge)
print("exp(1) =", math.exp(1))         -- 2.718281828459
print("log(e) =", math.log(math.exp(1)))   -- 1.0
print("log(100) =", math.log(100))     -- 4.6051701859881
print("log(100,10) =", math.log(100, 10))  -- 2.0
print("sqrt(2) =", math.sqrt(2))       -- 1.4142135623731
print("sin(pi/2) =", math.sin(math.pi/2))  -- 1.0
print("cos(0) =", math.cos(0))         -- 1.0
print("tan(0) =", math.tan(0))         -- 0.0
print("floor(3.7) =", math.floor(3.7)) -- 3
print("ceil(3.2) =", math.ceil(3.2))   -- 4
print("abs(-5) =", math.abs(-5))       -- 5
print("fmod(7,3) =", math.fmod(7, 3))  -- 1.0
print("modf(3.14) =", math.modf(3.14)) -- 0.14  3.0
print("pow(2,10) =", 2^10)  -- 1024.0
print("max:", math.max(1, 5, 3, 8, 2))  -- 8
print("min:", math.min(1, 5, 3, 8, 2))  -- 1

-- 整数边界
print("maxinteger:", math.maxinteger)
print("mininteger:", math.mininteger)
print("tointeger(3.0):", math.tointeger(3.0))   -- 3
print("tointeger(3.5):", math.tointeger(3.5))   -- nil

-- 随机数（确定性种子,可复现）
math.randomseed(42)
local r1 = math.random(1, 100)
local r2 = math.random(1, 100)
print("random1:", r1)
print("random2:", r2)

-- 三角函数恒等式验证
print("sin^2+cos^2 =", math.sin(1.2)^2 + math.cos(1.2)^2)  -- ~1.0

--[[
关键检查点:
- exp/log 互逆（log(exp(1)) ≈ 1.0）
- log(x, base) 双参数版（Lua 5.4）
- sin^2+cos^2 = 1（验证数学库精度）
- math.type / tointeger（整数子类型）
- math.maxinteger / mininteger（64位整数边界）
- randomseed + random 可复现
]]
