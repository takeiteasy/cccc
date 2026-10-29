local LIMIT = 10000000
local sieve = {}
for i = 0, LIMIT - 1 do sieve[i] = true end
sieve[0] = false
sieve[1] = false

local i = 2
while i * i < LIMIT do
    if sieve[i] then
        for j = i * i, LIMIT - 1, i do
            sieve[j] = false
        end
    end
    i = i + 1
end

local count, checksum = 0, 0
for k = 0, LIMIT - 1 do
    if sieve[k] then
        count = count + 1
        checksum = checksum + k
    end
end
print(string.format("result: count=%d checksum=%d", count, checksum))
os.exit(42)
