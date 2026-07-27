-- 06_string.lua  字符串库
-- 用法: lua 06_string.lua

local s = "Hello, RmikuOS!"
print("len:", #s)
print("upper:", string.upper(s))
print("lower:", string.lower(s))
print("sub(1,5):", string.sub(s, 1, 5))    -- Hello
print("sub(-7):", string.sub(s, -7))       -- RmikuOS
print("find:", string.find(s, "Rmiku"))    -- 8  13
print("rep:", string.rep("ab", 3))         -- ababab
print("reverse:", string.reverse("lua"))   -- aul
print("char:", string.char(65, 66, 67))    -- ABC
print("byte:", string.byte("A"))           -- 65

-- format
print(string.format("int=%d float=%.2f str=%s hex=%x", 42, 3.14159, "hi", 255))
-- int=42 float=3.14 str=hi hex=ff

-- gsub 替换
print(string.gsub("hello world", "o", "0"))     -- hell0 w0rld  2
print(string.gsub("a-b-c-d", "-", "+"))         -- a+b+c+d  3

-- gmatch 遍历
local words = {}
for w in string.gmatch("one,two,three,four", "[^,]+") do
    table.insert(words, w)
end
print("words:", table.concat(words, " "))   -- one two three four

-- match 捕获
local y, m, d = string.match("2026-07-28", "(%d+)-(%d+)-(%d+)")
print("date:", y, m, d)   -- 2026  07  28

-- split 模拟
local function split(str, sep)
    local r = {}
    for part in string.gmatch(str, "([^" .. sep .. "]+)") do
        table.insert(r, part)
    end
    return r
end
print("split:", table.concat(split("a:b:c:d", ":"), "|"))   -- a|b|c|d

--[[
关键检查点:
- 长度 / 大小写 / 子串 / 查找 / 复制 / 反转
- string.format（%d %.2f %s %x）—— 这个会压测你的 vsnprintf
- gsub 返回 (新串, 替换次数)
- gmatch + 模式（%d+ [^,]+）—— Lua 模式匹配,不是正则
- match 捕获组
]]
