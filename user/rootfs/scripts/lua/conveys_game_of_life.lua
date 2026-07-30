#!/usr/bin/env lua
-- life.lua
math.randomseed(os.time() or 1)

local W, H = 40, 20
local grid = {}
for y = 1, H do
    grid[y] = {}
    for x = 1, W do
        grid[y][x] = (math.random() < 0.3) and 1 or 0
    end
end

local function count_neighbors(g, x, y)
    local c = 0
    for dy = -1, 1 do
        for dx = -1, 1 do
            if dx ~= 0 or dy ~= 0 then
                local nx = ((x + dx - 1) % W) + 1
                local ny = ((y + dy - 1) % H) + 1
                c = c + g[ny][nx]
            end
        end
    end
    return c
end

local function step()
    local new = {}
    for y = 1, H do
        new[y] = {}
        for x = 1, W do
            local n = count_neighbors(grid, x, y)
            if grid[y][x] == 1 then
                new[y][x] = (n == 2 or n == 3) and 1 or 0
            else
                new[y][x] = (n == 3) and 1 or 0
            end
        end
    end
    grid = new
end

local function draw(gen)
    io.write("\27[2J\27[H")
    print("=== Conway's Game of Life ===  Gen: " .. gen)
    for y = 1, H do
        local line = ""
        for x = 1, W do
            line = line .. (grid[y][x] == 1 and "##" or "  ")
        end
        print(line)
    end
    print("Press Enter to step, Q+Enter to quit")
end

local gen = 0
draw(gen)
while true do
    io.write("> ")
    local c = io.read()
    if c == "q" or c == "Q" then break end
    step()
    gen = gen + 1
    draw(gen)
end