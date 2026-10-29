local n = 100000
local s = 0x9E3779B97F4A7C15
local arr = {}

local function mrand()
    s = s * 6364136223846793005 + 1442695040888963407
    return s >> 11
end

local function partition(lo, hi)
    local pivot = arr[hi]
    local i = lo - 1
    for j = lo, hi - 1 do
        if arr[j] < pivot then
            i = i + 1
            arr[i], arr[j] = arr[j], arr[i]
        end
    end
    arr[i + 1], arr[hi] = arr[hi], arr[i + 1]
    return i + 1
end

local function qs(lo, hi)
    if lo < hi then
        local p = partition(lo, hi)
        qs(lo, p - 1)
        qs(p + 1, hi)
    end
end

for i = 0, n - 1 do arr[i] = mrand() & 0x7FFFFFFF end
qs(0, n - 1)

local checksum = 0
for i = 0, n - 1 do checksum = checksum + arr[i] end
print(string.format("result: %d", checksum))
os.exit(42)
