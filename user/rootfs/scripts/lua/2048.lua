#!/usr/bin/env lua
-- 2048.lua
math.randomseed(os.time() or 1)

local board = {}
for i = 1, 4 do board[i] = {0,0,0,0} end

local function draw()
    -- 如果你的OS支持ANSI清屏就取消下面注释
    -- io.write("\27[2J\27[H")
    print("========== 2048 ==========")
    for i = 1, 4 do
        local line = ""
        for j = 1, 4 do
            local v = board[i][j]
            if v == 0 then
                line = line .. "  .   "
            else
                line = line .. string.format("%5d ", v)
            end
        end
        print(line)
    end
    print("W/A/S/D = 上/左/下/右 | Q = 退出")
    print("==========================")
end

local function add_random()
    local empty = {}
    for i = 1, 4 do
        for j = 1, 4 do
            if board[i][j] == 0 then table.insert(empty, {i,j}) end
        end
    end
    if #empty == 0 then return false end
    local p = empty[math.random(#empty)]
    board[p[1]][p[2]] = (math.random() < 0.9) and 2 or 4
    return true
end

local function slide(row)
    local new = {}
    for i = 1, 4 do
        if row[i] ~= 0 then table.insert(new, row[i]) end
    end
    for i = 1, #new-1 do
        if new[i] == new[i+1] then
            new[i] = new[i] * 2
            new[i+1] = 0
        end
    end
    local result = {}
    for i = 1, #new do
        if new[i] ~= 0 then table.insert(result, new[i]) end
    end
    while #result < 4 do table.insert(result, 0) end
    return result
end

local function move_left()
    local changed = false
    for i = 1, 4 do
        local old = {table.unpack(board[i])}
        board[i] = slide(board[i])
        for j = 1, 4 do
            if old[j] ~= board[i][j] then changed = true end
        end
    end
    return changed
end

local function rotate()
    local new = {}
    for i = 1, 4 do new[i] = {} end
    for i = 1, 4 do
        for j = 1, 4 do
            new[j][5-i] = board[i][j]
        end
    end
    board = new
end

local function move(dir)
    -- 0=左, 1=上, 2=右, 3=下；通过旋转统一用左移处理
    local changed = false
    for _ = 1, dir do rotate() end
    changed = move_left()
    for _ = 1, (4-dir)%4 do rotate() end
    return changed
end

local function can_move()
    for i = 1, 4 do
        for j = 1, 4 do
            if board[i][j] == 0 then return true end
            if j < 4 and board[i][j] == board[i][j+1] then return true end
            if i < 4 and board[i][j] == board[i+1][j] then return true end
        end
    end
    return false
end

add_random()
add_random()
draw()

while true do
    io.write("> ")
    local c = io.read(1)
    if not c then break end
    local ok = false
    if c == "w" or c == "W" then ok = move(1)
    elseif c == "a" or c == "A" then ok = move(0)
    elseif c == "s" or c == "S" then ok = move(3)
    elseif c == "d" or c == "D" then ok = move(2)
    elseif c == "q" or c == "Q" then print("Quit!"); break
    else ok = false end

    if ok then add_random() end
    draw()

    if not can_move() then
        print("GAME OVER!")
        break
    end
end