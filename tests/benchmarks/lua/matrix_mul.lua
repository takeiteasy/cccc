local n = 200
local s = 0x9E3779B97F4A7C15

local function mrand()
    s = s * 6364136223846793005 + 1442695040888963407
    return (s >> 11) / 9007199254740992.0
end

local a, b, c = {}, {}, {}
for i = 1, n * n do
    a[i] = mrand() - 0.5
    b[i] = mrand() - 0.5
    c[i] = 0.0
end

for i = 0, n - 1 do
    for k = 0, n - 1 do
        local aik = a[i * n + k + 1]
        local ci, bk = i * n, k * n
        for j = 1, n do
            c[ci + j] = c[ci + j] + aik * b[bk + j]
        end
    end
end

local checksum = 0.0
for i = 1, n * n do checksum = checksum + c[i] end
print(string.format("result: %.6f", checksum))
os.exit(42)
