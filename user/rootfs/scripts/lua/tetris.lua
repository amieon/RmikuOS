#!/usr/bin/env lua
-- tetris.lua
math.randomseed(os.time() or 1)

local W, H = 10, 20
local grid = {}
for y = 1, H do grid[y] = {}; for x = 1, W do grid[y][x] = 0 end end

local pieces = {
    { {1,1,1,1} },           -- I
    { {1,1,1}, {0,1,0} },    -- T
    { {1,1,1}, {1,0,0} },    -- L
    { {1,1,1}, {0,0,1} },    -- J
    { {1,1}, {1,1} },        -- O
    { {0,1,1}, {1,1,0} },    -- S
    { {1,1,0}, {0,1,1} },    -- Z
}

local function new_piece()
    local p = pieces[math.random(#pieces)]
    local piece = { shape = {}, x = 4, y = 1 }
    for i = 1, #p do
        piece.shape[i] = {}
        for j = 1, #p[i] do
            piece.shape[i][j] = p[i][j]
        end
    end
    return piece
end

local cur = new_piece()
local score = 0

local function draw()
    io.write("\27[2J\27[H") -- 清屏，不支持的话注释掉
    print("====== TETRIS ======  Score: " .. score)
    for y = 1, H do
        local line = "|"
        for x = 1, W do
            local occupied = (grid[y][x] ~= 0)
            if not occupied then
                for py = 1, #cur.shape do
                    for px = 1, #cur.shape[py] do
                        if cur.shape[py][px] ~= 0 then
                            if cur.x + px - 1 == x and cur.y + py - 1 == y then
                                occupied = true
                            end
                        end
                    end
                end
            end
            line = line .. (occupied and "[]" or " .")
        end
        print(line .. "|")
    end
    print("+--------------------+")
    print("A=左 D=右 S=下 W=转 Q=退出")
end

local function rotate(p)
    local new = {}
    local rows = #p.shape
    local cols = #p.shape[1]
    for c = 1, cols do
        new[c] = {}
        for r = 1, rows do
            new[c][r] = p.shape[rows - r + 1][c]
        end
    end
    return { shape = new, x = p.x, y = p.y }
end

local function valid(p)
    for py = 1, #p.shape do
        for px = 1, #p.shape[py] do
            if p.shape[py][px] ~= 0 then
                local gx = p.x + px - 1
                local gy = p.y + py - 1
                if gx < 1 or gx > W or gy > H then return false end
                if gy >= 1 and grid[gy][gx] ~= 0 then return false end
            end
        end
    end
    return true
end

local function lock()
    for py = 1, #cur.shape do
        for px = 1, #cur.shape[py] do
            if cur.shape[py][px] ~= 0 then
                local gy = cur.y + py - 1
                if gy >= 1 then grid[gy][cur.x + px - 1] = 1 end
            end
        end
    end
    -- 消行
    for y = H, 1, -1 do
        local full = true
        for x = 1, W do
            if grid[y][x] == 0 then full = false; break end
        end
        if full then
            table.remove(grid, y)
            local newrow = {}
            for x = 1, W do newrow[x] = 0 end
            table.insert(grid, 1, newrow)
            score = score + 100
        end
    end
    cur = new_piece()
    if not valid(cur) then
        print("GAME OVER! Score: " .. score)
        os.exit(0)
    end
end

draw()
while true do
    io.write("> ")
    local c = io.read(1)
    if not c then break end
    local next = cur

    if c == "a" or c == "A" then
        next = { shape = cur.shape, x = cur.x - 1, y = cur.y }
    elseif c == "d" or c == "D" then
        next = { shape = cur.shape, x = cur.x + 1, y = cur.y }
    elseif c == "s" or c == "S" then
        next = { shape = cur.shape, x = cur.x, y = cur.y + 1 }
    elseif c == "w" or c == "W" then
        next = rotate(cur)
    elseif c == "q" or c == "Q" then
        print("Quit!"); break
    elseif c == " " then
        -- 硬降
        next = { shape = cur.shape, x = cur.x, y = cur.y + 1 }
        while valid(next) do
            cur = next
            next = { shape = cur.shape, x = cur.x, y = cur.y + 1 }
        end
        lock()
    end

    if c ~= " " then
        if valid(next) then cur = next end
        if (c == "s" or c == "S") and not valid(next) then lock() end
    end
    draw()
end