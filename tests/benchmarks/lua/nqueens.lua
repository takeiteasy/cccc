local N = 10
local total = 0
local cols = {}

local function solve(n, row)
    if row == n then
        total = total + 1
        return
    end
    for c = 0, n - 1 do
        local ok = true
        for r = 0, row - 1 do
            local d = cols[r] - c
            if d < 0 then d = -d end
            if cols[r] == c or d == row - r then
                ok = false
                break
            end
        end
        if ok then
            cols[row] = c
            solve(n, row + 1)
        end
    end
end

solve(N, 0)
print(string.format("result: %d", total))
os.exit(42)
