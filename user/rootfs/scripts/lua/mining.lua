#!/usr/bin/env lua
-- minesweeper.lua
math.randomseed(os.time() or 1)

local W, H, MINES = 10, 10, 15
local board = {}   -- -1 = mine, 0-8 = count
local visible = {} -- 0 = hidden, 1 = shown, 2 = flag
local mines_placed = false

local function init_board()
    for y = 1, H do
        board[y] = {}; visible[y] = {}
        for x = 1, W do
            board[y][x] = 0
            visible[y][x] = 0
        end
    end
end

local function place_mines(sx, sy)
    local placed = 0
    while placed < MINES do
        local x = math.random(W)
        local y = math.random(H)
        if board[y][x] ~= -1 and not (x == sx and y == sy) then
            board[y][x] = -1
            placed = placed + 1
        end
    end
    for y = 1, H do
        for x = 1, W do
            if board[y][x] ~= -1 then
                local c = 0
                for dy = -1, 1 do
                    for dx = -1, 1 do
                        local nx, ny = x+dx, y+dy
                        if nx >= 1 and nx <= W and ny >= 1 and ny <= H then
                            if board[ny][nx] == -1 then c = c + 1 end
                        end
                    end
                end
                board[y][x] = c
            end
        end
    end
    mines_placed = true
end

local function reveal(x, y)
    if x < 1 or x > W or y < 1 or y > H then return end
    if visible[y][x] ~= 0 then return end
    visible[y][x] = 1
    if board[y][x] == 0 then
        for dy = -1, 1 do
            for dx = -1, 1 do
                reveal(x+dx, y+dy)
            end
        end
    end
end

local function draw()
    io.write("\27[2J\27[H")
    print("==== MINESWEEPER ====  Mines: " .. MINES)
    io.write("   ")
    for x = 1, W do io.write(string.format("%2d ", x)) end
    print()
    for y = 1, H do
        io.write(string.format("%2d ", y))
        for x = 1, W do
            local v = visible[y][x]
            if v == 2 then
                io.write(" F ")
            elseif v == 0 then
                io.write(" . ")
            else
                local b = board[y][x]
                if b == -1 then io.write(" * ")
                elseif b == 0 then io.write("   ")
                else io.write(string.format(" %d ", b)) end
            end
        end
        print()
    end
    print("Enter: X Y [f]  (e.g. '5 3' to open, '5 3 f' to flag)")
end

local function check_win()
    local hidden = 0
    for y = 1, H do
        for x = 1, W do
            if visible[y][x] == 0 then hidden = hidden + 1 end
        end
    end
    return hidden == MINES
end

init_board()
draw()

while true do
    io.write("> ")
    local line = io.read()
    if not line then break end
    local x, y, f = line:match("(%d+)%s+(%d+)%s*(%a*)")
    x = tonumber(x); y = tonumber(y)
    if not x or not y then print("Invalid input"); goto continue end

    if not mines_placed then place_mines(x, y) end

    if f == "f" or f == "F" then
        if visible[y][x] == 0 then visible[y][x] = 2
        elseif visible[y][x] == 2 then visible[y][x] = 0 end
    else
        if board[y][x] == -1 then
            for yy = 1, H do for xx = 1, W do visible[yy][xx] = 1 end end
            draw()
            print("BOOM! You hit a mine.")
            break
        end
        reveal(x, y)
    end

    draw()
    if check_win() then
        print("YOU WIN!")
        break
    end
    ::continue::
end