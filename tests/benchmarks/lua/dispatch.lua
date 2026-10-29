local OP_ADD, OP_MUL, OP_XOR, OP_ROT, OP_DEC, OP_LOOP, OP_HALT = 0, 1, 2, 3, 4, 5, 6
local code = { OP_ADD, OP_MUL, OP_XOR, OP_ROT, OP_ADD, OP_DEC, OP_LOOP, OP_HALT }

local acc = 1
local left = 1000000
local pc = 1
while true do
    local op = code[pc]
    pc = pc + 1
    if op == OP_ADD then
        acc = acc + 0x9E3779B9
    elseif op == OP_MUL then
        acc = acc * 6364136223846793005
    elseif op == OP_XOR then
        acc = acc ~ (acc >> 29)
    elseif op == OP_ROT then
        acc = (acc >> 3) | (acc << 61)
    elseif op == OP_DEC then
        left = left - 1
    elseif op == OP_LOOP then
        if left > 0 then pc = 1 end
    else
        break
    end
end
print(string.format("result: %u", acc))
os.exit(42)
