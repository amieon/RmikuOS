-- 09_metatable.lua  元表 + 元方法 + 面向对象
-- 用法: lua 09_metatable.lua

-- 向量类（运算符重载）
local Vec = {}
Vec.__index = Vec

function Vec.new(x, y)
    return setmetatable({x=x, y=y}, Vec)
end

function Vec.__add(a, b)     -- +
    return Vec.new(a.x + b.x, a.y + b.y)
end
function Vec.__sub(a, b)     -- -
    return Vec.new(a.x - b.x, a.y - b.y)
end
function Vec.__eq(a, b)      -- ==
    return a.x == b.x and a.y == b.y
end
function Vec.__tostring(a)   -- tostring()
    return string.format("(%d,%d)", a.x, a.y)
end
function Vec.__len(a)        -- #（Lua 5.4）
    return math.floor(math.sqrt(a.x*a.x + a.y*a.y))
end
function Vec:magnitude()
    return math.sqrt(self.x^2 + self.y^2)
end

local v1 = Vec.new(3, 4)
local v2 = Vec.new(1, 2)
print("v1:", tostring(v1))           -- (3,4)
print("v2:", tostring(v2))           -- (1,2)
print("v1+v2:", tostring(v1 + v2))   -- (4,6)
print("v1-v2:", tostring(v1 - v2))   -- (2,2)
print("v1==v2:", v1 == v2)           -- false
print("v1==Vec(3,4):", v1 == Vec.new(3,4))  -- true
print("|v1|:", #v1)                  -- 5
print("magnitude:", v1:magnitude())  -- 5.0

-- __index 实现继承
local Animal = {legs=4}
Animal.__index = Animal
function Animal:speak() return "..." end

local Dog = setmetatable({}, {__index = Animal})
Dog.__index = Dog
function Dog:speak() return "woof" end
function Dog:fetch() return "fetching" end

local d = setmetatable({}, Dog)
print("dog legs:", d.legs)        -- 4 (继承自 Animal)
print("dog speak:", d:speak())    -- woof
print("dog fetch:", d:fetch())    -- fetching

-- __call（把 table 当函数调用）
local counter = setmetatable({n=0}, {
    __call = function(self, inc)
        self.n = self.n + (inc or 1)
        return self.n
    end
})
print(counter())     -- 1
print(counter(10))   -- 11
print(counter())     -- 12

-- __index 作为函数（属性访问钩子）
local proxy = setmetatable({}, {
    __index = function(t, k)
        return "missing:" .. k
    end,
    __newindex = function(t, k, v)
        rawset(t, k, v .. "_stored")
    end
})
print(proxy.foo)           -- missing:foo
proxy.bar = "x"
print(rawget(proxy, "bar"))  -- x_stored

--[[
关键检查点:
- __add / __sub / __eq / __tostring / __len 运算符重载
- __index 实现方法查找 + 继承链
- __call 让 table 可调用
- __index / __newindex 作为函数（属性代理）
- rawget / rawset 绕过元方法
- self / method 语法（obj:method()）
]]
