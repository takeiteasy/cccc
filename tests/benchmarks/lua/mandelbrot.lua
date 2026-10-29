local w, h, max_iter = 400, 400, 200
local total = 0
for py = 0, h - 1 do
    local y0 = py / h * 2.5 - 1.25
    for px = 0, w - 1 do
        local x0 = px / w * 2.0 - 2.0
        local x, y = 0.0, 0.0
        local iter = 0
        while iter < max_iter and x * x + y * y <= 4.0 do
            local xt = x * x - y * y + x0
            y = 2.0 * x * y + y0
            x = xt
            iter = iter + 1
        end
        total = total + iter
    end
end
print(string.format("result: %d", total))
os.exit(42)
