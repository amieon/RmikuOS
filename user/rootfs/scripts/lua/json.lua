-- json.lua —— 纯 Lua JSON 解析器，可直接复制进你的 OS
local json = {}

local function skip_ws(s, i)
    while s:sub(i,i):match("[%s]") do i = i + 1 end
    return i
end

local function parse_str(s, i)
    i = i + 1
    local res = ""
    while i <= #s do
        local c = s:sub(i,i)
        if c == '"' then return res, i + 1 end
        if c == "\\" then
            i = i + 1
            local n = s:sub(i,i)
            if n == "n" then res = res .. "\n"
            elseif n == "t" then res = res .. "\t"
            elseif n == "r" then res = res .. "\r"
            elseif n == "b" then res = res .. "\b"
            elseif n == "f" then res = res .. "\f"
            else res = res .. n end
        else
            res = res .. c
        end
        i = i + 1
    end
    error("unterminated string")
end

local function parse_num(s, i)
    local j = i
    while s:sub(j,j):match("[%d%.%-eE%+]") do j = j + 1 end
    return tonumber(s:sub(i, j-1)), j
end

local parse_val
function parse_val(s, i)
    i = skip_ws(s, i)
    local c = s:sub(i,i)
    if c == '"' then return parse_str(s, i)
    elseif c == "{" then
        local obj = {}
        i = skip_ws(s, i + 1)
        if s:sub(i,i) == "}" then return obj, i + 1 end
        while true do
            local key, ni = parse_str(s, i)
            i = skip_ws(s, ni)
            if s:sub(i,i) ~= ":" then error("expected :") end
            local val, n2 = parse_val(s, i + 1)
            obj[key] = val
            i = skip_ws(s, n2)
            local d = s:sub(i,i)
            if d == "}" then return obj, i + 1 end
            if d ~= "," then error("expected , or }") end
            i = skip_ws(s, i + 1)
        end
    elseif c == "[" then
        local arr = {}
        i = skip_ws(s, i + 1)
        if s:sub(i,i) == "]" then return arr, i + 1 end
        while true do
            local val, ni = parse_val(s, i)
            table.insert(arr, val)
            i = skip_ws(s, ni)
            local d = s:sub(i,i)
            if d == "]" then return arr, i + 1 end
            if d ~= "," then error("expected , or ]") end
            i = skip_ws(s, i + 1)
        end
    elseif c == "t" and s:sub(i, i+3) == "true" then return true, i + 4
    elseif c == "f" and s:sub(i, i+4) == "false" then return false, i + 5
    elseif c == "n" and s:sub(i, i+3) == "null" then return nil, i + 4
    else return parse_num(s, i) end
end

function json.decode(s)
    local val, i = parse_val(s, 1)
    return val
end

function json.encode(v)
    local t = type(v)
    if t == "nil" then return "null"
    elseif t == "boolean" then return v and "true" or "false"
    elseif t == "number" then return tostring(v)
    elseif t == "string" then return '"' .. v:gsub('[%c\\"]', function(c)
        if c == "\n" then return "\\n"
        elseif c == "\t" then return "\\t"
        elseif c == "\r" then return "\\r"
        elseif c == '"' then return '\\"'
        elseif c == "\\" then return "\\\\"
        else return string.format("\\u%04x", c:byte()) end
    end) .. '"'
    elseif t == "table" then
        local is_arr = #v > 0
        if is_arr then
            local parts = {}
            for _, item in ipairs(v) do table.insert(parts, json.encode(item)) end
            return "[" .. table.concat(parts, ",") .. "]"
        else
            local parts = {}
            for k, val in pairs(v) do
                table.insert(parts, json.encode(tostring(k)) .. ":" .. json.encode(val))
            end
            return "{" .. table.concat(parts, ",") .. "}"
        end
    end
end

-- 测试
if arg and arg[0] and arg[0]:match("json%.lua$") then
    local test = '{"name":"RmikuOS","version":1.0,"features":["TCP","Lua","RISC-V"]}'
    local obj = json.decode(test)
    print("OS Name: " .. obj.name)
    print("Features: " .. table.concat(obj.features, ", "))
    print("Encoded: " .. json.encode({cpu="RISC-V", uptime=3600}))
end

return json